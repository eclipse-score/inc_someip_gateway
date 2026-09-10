/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "bridged_service_apps.hpp"
#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/socom/runtime.hpp"

namespace score::gateway_ipc_binding::mw_com {
namespace {

using test::Consumer_app;
using test::Producer_app;

/// \brief SomeipdService instance used by the bridged service tests
constexpr char const* kSomeipd_specifier = "someipd/daemon";

/// \brief Bridged service instance used by the bridged service tests
/// \details Every test may reuse it: a `Bridge` tears its skeleton, proxy and applications down
///          again before the next test constructs its own, so no state survives a test.
constexpr char const* kBridged_specifier = "ipc/bridged";

/// \brief Names of the two events the test service type declares in mw_com_config.json
constexpr char const* kEvent_a = "event_a";
constexpr char const* kEvent_b = "event_b";

/// \brief What a SOME/IP header costs, so that the reserved space is realistic
constexpr std::size_t kHeader_size = 16U;
constexpr std::size_t kMax_payload_size = 128U;
constexpr std::size_t kMax_sample_count = 4U;

/// \brief SOCom identity of the bridged service, the same on both sides of the link
socom::Service_interface_identifier test_interface() {
    return socom::Service_interface_identifier{
        std::string_view{"/test/ipc/BridgedService"},
        socom::Service_interface_identifier::Version{1U, 0U}};
}

socom::Service_instance test_instance() { return socom::Service_instance{std::string_view{"1"}}; }

std::vector<Event_config> test_events() {
    return {Event_config{kEvent_a, kHeader_size, kMax_payload_size},
            Event_config{kEvent_b, kHeader_size, kMax_payload_size}};
}

Service_config test_service(Role const role) {
    return Service_config{test_interface(), test_instance(),  kBridged_specifier, role,
                          test_events(),    kMax_sample_count};
}

std::vector<std::byte> bytes(std::initializer_list<int> const values) {
    std::vector<std::byte> result{};
    result.reserve(values.size());
    for (auto const value : values) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

std::vector<std::byte> filled(std::size_t const size, int const first) {
    std::vector<std::byte> result{};
    result.reserve(size);
    for (std::size_t index = 0U; index < size; ++index) {
        result.push_back(static_cast<std::byte>((first + static_cast<int>(index)) & 0xFF));
    }
    return result;
}

/// \brief Two SOCom runtimes in one process, standing in for the two daemons.
/// \details `m_server_runtime` is the `someipd` side, `m_client_runtime` the `gatewayd` side.
///          Both halves of the mw::com link live in this process, exactly as they would across
///          two processes.
class Bridged_service_test : public ::testing::Test {
   protected:
    socom::Runtime::Uptr m_client_runtime = score::socom::create_runtime();
    socom::Runtime::Uptr m_server_runtime = score::socom::create_runtime();
};

/// \brief The whole link for one bridged service instance, in the order the daemons build it.
/// \details Members are destroyed bottom up, so the applications go away before the bindings,
///          which is the order the SOCom deadlock detector expects.
struct Bridge {
    Bridge(socom::Runtime& provider_runtime, socom::Runtime& consumer_runtime)
        : server{create_server(provider_runtime, kSomeipd_specifier,
                               {test_service(Role::provider)})},
          consumer_runtime_ref{consumer_runtime},
          provider_runtime_ref{provider_runtime} {}

    /// \brief Bring the link up, mirroring `someipd` starting before `gatewayd` connects
    /// \param unsubscribe_on_update Whether the consuming application unsubscribes from within
    ///        its own on_event_update callback
    [[nodiscard]] bool start() {
        if ((server == nullptr) || !server->start().has_value()) {
            return false;
        }
        client =
            create_client(consumer_runtime_ref, kSomeipd_specifier, {test_service(Role::consumer)});
        if (client == nullptr) {
            return false;
        }
        producer = std::make_unique<Producer_app>(provider_runtime_ref, test_interface(),
                                                  test_instance(), 2U);
        consumer =
            std::make_unique<Consumer_app>(consumer_runtime_ref, test_interface(), test_instance());
        return producer->is_valid() && consumer->is_valid();
    }

    std::unique_ptr<Gateway_ipc_binding_server> server;
    std::unique_ptr<Gateway_ipc_binding_client> client{};
    std::unique_ptr<Producer_app> producer{};
    std::unique_ptr<Consumer_app> consumer{};

    socom::Runtime& consumer_runtime_ref;
    socom::Runtime& provider_runtime_ref;
};

TEST_F(Bridged_service_test, service_availability_is_propagated_to_the_consumer_side) {
    Bridge bridge{*m_server_runtime, *m_client_runtime};
    ASSERT_TRUE(bridge.start());

    // The producing application offering its SOCom service is what makes the GenericSkeleton be
    // offered, which in turn is what enables the SOCom server connector on the other side.
    EXPECT_TRUE(bridge.consumer->wait_for_availability(true));
}

TEST_F(Bridged_service_test, subscription_is_propagated_to_the_provider_side) {
    Bridge bridge{*m_server_runtime, *m_client_runtime};
    ASSERT_TRUE(bridge.start());
    ASSERT_TRUE(bridge.consumer->wait_for_availability(true));

    // Nobody wants the event yet, so no subscription must have reached the producer.
    EXPECT_FALSE(bridge.producer->wait_for_subscription(0U, true, test::kApp_negative_timeout));

    ASSERT_TRUE(bridge.consumer->subscribe(0U));
    EXPECT_TRUE(bridge.producer->wait_for_subscription(0U, true));

    // The second event stays unwanted: subscriptions are propagated per event.
    EXPECT_FALSE(bridge.producer->wait_for_subscription(1U, true, test::kApp_negative_timeout));

    ASSERT_TRUE(bridge.consumer->unsubscribe(0U));
    EXPECT_TRUE(bridge.producer->wait_for_subscription(0U, false));
}

TEST_F(Bridged_service_test, event_header_and_payload_survive_the_round_trip) {
    Bridge bridge{*m_server_runtime, *m_client_runtime};
    ASSERT_TRUE(bridge.start());
    ASSERT_TRUE(bridge.consumer->wait_for_availability(true));
    ASSERT_TRUE(bridge.consumer->subscribe(0U));
    ASSERT_TRUE(bridge.producer->wait_for_subscription(0U, true));

    auto const header = filled(kHeader_size, 0x40);
    auto const data = bytes({1, 2, 3, 4, 5});
    ASSERT_TRUE(bridge.producer->send(0U, header, data));

    ASSERT_TRUE(bridge.consumer->wait_for_events(1U));
    auto const received = bridge.consumer->take_received();
    ASSERT_EQ(received.size(), 1U);
    EXPECT_EQ(received[0].event_id, 0U);
    // The SOME/IP header travels contiguously in front of the payload, so that E2E can be
    // computed over both without an extra copy.
    EXPECT_EQ(received[0].header, header);
    // Only the bytes the producer shrank the payload to, not the whole fixed-size slot.
    EXPECT_EQ(received[0].data, data);
}

TEST_F(Bridged_service_test, an_empty_and_a_full_payload_both_survive_the_round_trip) {
    Bridge bridge{*m_server_runtime, *m_client_runtime};
    ASSERT_TRUE(bridge.start());
    ASSERT_TRUE(bridge.consumer->wait_for_availability(true));
    ASSERT_TRUE(bridge.consumer->subscribe(0U));
    ASSERT_TRUE(bridge.producer->wait_for_subscription(0U, true));

    auto const header = filled(kHeader_size, 0x10);
    auto const full = filled(kMax_payload_size, 0x80);
    ASSERT_TRUE(bridge.producer->send(0U, header, {}));
    ASSERT_TRUE(bridge.producer->send(0U, header, full));

    ASSERT_TRUE(bridge.consumer->wait_for_events(2U));
    auto const received = bridge.consumer->take_received();
    ASSERT_EQ(received.size(), 2U);
    EXPECT_TRUE(received[0].data.empty());
    EXPECT_EQ(received[1].data, full);
}

TEST_F(Bridged_service_test, both_events_of_a_service_are_bridged_independently) {
    Bridge bridge{*m_server_runtime, *m_client_runtime};
    ASSERT_TRUE(bridge.start());
    ASSERT_TRUE(bridge.consumer->wait_for_availability(true));
    ASSERT_TRUE(bridge.consumer->subscribe(0U));
    ASSERT_TRUE(bridge.consumer->subscribe(1U));
    ASSERT_TRUE(bridge.producer->wait_for_subscription(0U, true));
    ASSERT_TRUE(bridge.producer->wait_for_subscription(1U, true));

    auto const header = filled(kHeader_size, 0x20);
    ASSERT_TRUE(bridge.producer->send(0U, header, bytes({0xAA})));
    ASSERT_TRUE(bridge.producer->send(1U, header, bytes({0xBB, 0xCC})));

    ASSERT_TRUE(bridge.consumer->wait_for_events(2U));
    auto received = bridge.consumer->take_received();
    ASSERT_EQ(received.size(), 2U);
    std::sort(received.begin(), received.end(),
              [](auto const& lhs, auto const& rhs) { return lhs.event_id < rhs.event_id; });
    EXPECT_EQ(received[0].event_id, 0U);
    EXPECT_EQ(received[0].data, bytes({0xAA}));
    EXPECT_EQ(received[1].event_id, 1U);
    EXPECT_EQ(received[1].data, bytes({0xBB, 0xCC}));
}

TEST_F(Bridged_service_test, the_service_becomes_unavailable_when_the_producer_stops) {
    Bridge bridge{*m_server_runtime, *m_client_runtime};
    ASSERT_TRUE(bridge.start());
    ASSERT_TRUE(bridge.consumer->wait_for_availability(true));
    ASSERT_TRUE(bridge.consumer->subscribe(0U));
    ASSERT_TRUE(bridge.producer->wait_for_subscription(0U, true));

    // Tearing the producing application down while a subscription is live is the interesting
    // case: it stops the offer with samples still owned on the far side.
    bridge.producer.reset();

    EXPECT_TRUE(bridge.consumer->wait_for_availability(false));
}

TEST_F(Bridged_service_test, no_event_is_forwarded_after_the_consumer_unsubscribes) {
    Bridge bridge{*m_server_runtime, *m_client_runtime};
    ASSERT_TRUE(bridge.start());
    ASSERT_TRUE(bridge.consumer->wait_for_availability(true));
    ASSERT_TRUE(bridge.consumer->subscribe(0U));
    ASSERT_TRUE(bridge.producer->wait_for_subscription(0U, true));

    auto const header = filled(kHeader_size, 0x50);
    ASSERT_TRUE(bridge.producer->send(0U, header, bytes({1})));
    ASSERT_TRUE(bridge.consumer->wait_for_events(1U));
    ASSERT_EQ(bridge.consumer->take_received().size(), 1U);

    ASSERT_TRUE(bridge.consumer->unsubscribe(0U));
    ASSERT_TRUE(bridge.producer->wait_for_subscription(0U, false));

    // The producing application is not asked for data anymore, so there is nothing left to
    // allocate a payload from.
    EXPECT_FALSE(bridge.producer->send(0U, header, bytes({2})));
    EXPECT_FALSE(bridge.consumer->wait_for_events(1U, test::kApp_negative_timeout));
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
