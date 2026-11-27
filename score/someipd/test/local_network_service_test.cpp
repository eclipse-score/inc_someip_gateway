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

#include "score/someipd/impl/local_network_service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "score/config/mw_someip_config_generated.h"
#include "score/socom/callback_mocks.hpp"
#include "score/socom/runtime.hpp"

using ::testing::_;

namespace score::someipd {
namespace {

// FlatBuffers needs a null-terminated C string, SOCom a string_view of the same name.
constexpr char const kServiceTypeName[] = "test_service";
constexpr std::string_view kServiceTypeNameView{kServiceTypeName};
constexpr std::uint8_t kVersionMajor = 1;
constexpr std::uint32_t kVersionMinor = 0;
constexpr std::size_t kNumEvents = 2;

class LocalNetworkServiceTest : public ::testing::Test {
   protected:
    LocalNetworkServiceTest() {
        flatbuffers::FlatBufferBuilder builder;
        std::vector<flatbuffers::Offset<mw_someip_config::Event>> events{
            mw_someip_config::CreateEventDirect(builder, 1, "event_a"),
            mw_someip_config::CreateEventDirect(builder, 2, "event_b")};
        std::vector<flatbuffers::Offset<mw_someip_config::ServiceInstance>> instances{
            mw_someip_config::CreateServiceInstanceDirect(builder, "test/instance", 1)};
        std::vector<flatbuffers::Offset<mw_someip_config::ServiceType>> service_types{
            mw_someip_config::CreateServiceTypeDirect(builder, kServiceTypeName, 0x1234,
                                                      kVersionMajor, kVersionMinor, &events,
                                                      nullptr, &instances)};
        builder.Finish(mw_someip_config::CreateRootDirect(builder, &service_types));

        buffer_ = std::make_shared<std::vector<std::uint8_t>>(
            builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        auto const* root = mw_someip_config::GetRoot(buffer_->data());
        service_type_config_ = std::shared_ptr<const mw_someip_config::ServiceType>(
            buffer_, root->service_types()->Get(0));
        service_instance_config_ = std::shared_ptr<const mw_someip_config::ServiceInstance>(
            buffer_, service_type_config_->local_service_instances()->Get(0));
    }

    /// Creates the server connector that LocalNetworkService connects to (in production it is
    /// provided by gatewayd via the IPC binding).
    socom::Disabled_server_connector::Uptr make_server_connector() {
        // The config stores the minor version as uint32, SOCom as uint16; kVersionMinor fits both.
        socom::Server_service_interface_definition const config{
            socom::Service_interface_identifier{
                kServiceTypeNameView, {kVersionMajor, static_cast<std::uint16_t>(kVersionMinor)}},
            socom::to_num_of_methods(0), socom::to_num_of_events(kNumEvents)};
        auto result = runtime_->make_server_connector(
            config, socom::Service_instance{kServiceTypeNameView},
            socom::Disabled_server_connector::Callbacks{
                method_call_mock_.as_function(), event_subscription_change_mock_.as_function(),
                event_update_request_mock_.as_function(),
                method_payload_allocate_mock_.as_function()});
        if (!result.has_value()) {
            ADD_FAILURE() << "make_server_connector() failed";
            return nullptr;
        }
        return std::move(result).value();
    }

    void expect_all_events_subscribed() {
        // Destroying the LocalNetworkService before the server unsubscribes its events.
        EXPECT_CALL(event_subscription_change_mock_, Call(_, _, socom::Event_state::unsubscribed))
            .Times(::testing::AnyNumber());
        for (std::size_t i = 0; i < kNumEvents; ++i) {
            EXPECT_CALL(event_subscription_change_mock_,
                        Call(_, static_cast<socom::Event_id>(i), socom::Event_state::subscribed))
                .Times(1);
        }
    }

    Result<std::unique_ptr<LocalNetworkService>> create_service() {
        return LocalNetworkService::Create(service_instance_config_, service_type_config_, nullptr,
                                           set_offered_mock_.AsStdFunction(), *runtime_);
    }

    std::shared_ptr<std::vector<std::uint8_t>> buffer_;
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config_;
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config_;

    socom::Runtime::Uptr runtime_ = socom::create_runtime();

    socom::Method_call_callback_mock method_call_mock_;
    socom::Event_subscription_change_callback_mock event_subscription_change_mock_;
    socom::Event_request_update_callback_mock event_update_request_mock_;
    socom::Method_call_payload_allocate_callback_mock method_payload_allocate_mock_;
    ::testing::NiceMock<::testing::MockFunction<void(bool)>> set_offered_mock_;
};

// Regression test for the race behind issue #305: if the server connector is already available,
// SOCom invokes on_service_state_change(available) synchronously inside make_client_connector(),
// i.e. before LocalNetworkService::Create() could store the returned client connector. The
// callback must still subscribe to all events (and must not dereference the not yet stored
// connector).
TEST_F(LocalNetworkServiceTest, SubscribesAllEventsWhenServiceIsAlreadyAvailable) {
    auto server_connector = make_server_connector();
    ASSERT_NE(server_connector, nullptr);
    auto const server = socom::Disabled_server_connector::enable(std::move(server_connector));
    expect_all_events_subscribed();

    auto const service = create_service();

    ASSERT_TRUE(service.has_value());
}

TEST_F(LocalNetworkServiceTest, SubscribesAllEventsWhenServiceBecomesAvailableLater) {
    auto const service = create_service();
    ASSERT_TRUE(service.has_value());
    auto server_connector = make_server_connector();
    ASSERT_NE(server_connector, nullptr);
    expect_all_events_subscribed();

    auto const server = socom::Disabled_server_connector::enable(std::move(server_connector));
}

// Issue #366: the SOME/IP offer follows the availability of the bridged service. It is withdrawn
// when the provider or its gatewayd goes away, and comes back with the service.
TEST_F(LocalNetworkServiceTest, OffersOnlyWhileTheServiceIsAvailable) {
    EXPECT_CALL(event_subscription_change_mock_, Call(_, _, _)).Times(::testing::AnyNumber());
    ::testing::MockFunction<void()> checkpoint;
    {
        ::testing::InSequence const sequence;
        EXPECT_CALL(set_offered_mock_, Call(true));
        EXPECT_CALL(checkpoint, Call());
        EXPECT_CALL(set_offered_mock_, Call(false));
        EXPECT_CALL(checkpoint, Call());
        EXPECT_CALL(set_offered_mock_, Call(true));
        EXPECT_CALL(checkpoint, Call());
        // Destroying the service withdraws the offer.
        EXPECT_CALL(set_offered_mock_, Call(false));
    }

    auto service = create_service();
    ASSERT_TRUE(service.has_value());
    auto server_connector = make_server_connector();
    ASSERT_NE(server_connector, nullptr);

    auto server = socom::Disabled_server_connector::enable(std::move(server_connector));
    checkpoint.Call();
    auto disabled = socom::Enabled_server_connector::disable(std::move(server));
    checkpoint.Call();
    server = socom::Disabled_server_connector::enable(std::move(disabled));
    checkpoint.Call();
    service.value().reset();
}

TEST_F(LocalNetworkServiceTest, NeverOffersAServiceThatIsNotAvailable) {
    EXPECT_CALL(set_offered_mock_, Call(true)).Times(0);
    EXPECT_CALL(set_offered_mock_, Call(false));  // on destruction; Routing ignores it

    auto service = create_service();
    ASSERT_TRUE(service.has_value());
    service.value().reset();
}

TEST_F(LocalNetworkServiceTest, OffersAServiceThatIsAlreadyAvailable) {
    EXPECT_CALL(event_subscription_change_mock_, Call(_, _, _)).Times(::testing::AnyNumber());
    auto server_connector = make_server_connector();
    ASSERT_NE(server_connector, nullptr);
    auto const server = socom::Disabled_server_connector::enable(std::move(server_connector));
    {
        ::testing::InSequence const sequence;
        EXPECT_CALL(set_offered_mock_, Call(true));
        EXPECT_CALL(set_offered_mock_, Call(false));  // on destruction of the service
    }

    auto const service = create_service();

    ASSERT_TRUE(service.has_value());
}

}  // namespace
}  // namespace score::someipd
