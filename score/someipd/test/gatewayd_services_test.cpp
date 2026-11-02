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

#include "score/someipd/impl/gatewayd_services.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "score/config/mw_someip_config_generated.h"
#include "score/socom/callback_mocks.hpp"
#include "score/socom/runtime.hpp"
#include "score/someipd/impl/routing.h"

namespace score::someipd {
namespace {

using gateway_ipc_binding::mw_com::Role;
using gateway_ipc_binding::mw_com::Service_configs;

/// \brief One service type of a gateway configuration
struct Service {
    char const* name;
    std::uint16_t service_id;
    /// \brief Offered to the network if true, consumed from it otherwise
    bool local;
    std::uint16_t event_id;
};

/// \brief mw_someip_config flatbuffer with one event and one instance per service
std::vector<std::byte> gateway_config(std::vector<Service> const& services) {
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<mw_someip_config::ServiceType>> service_types{};
    for (auto const& service : services) {
        std::vector<flatbuffers::Offset<mw_someip_config::Event>> events{
            mw_someip_config::CreateEventDirect(builder, service.event_id, "event_a")};
        std::vector<flatbuffers::Offset<mw_someip_config::ServiceInstance>> instances{
            mw_someip_config::CreateServiceInstanceDirect(builder, "test/instance", 1U)};
        service_types.push_back(mw_someip_config::CreateServiceTypeDirect(
            builder, service.name, service.service_id, 1U, 0U, &events, nullptr,
            service.local ? &instances : nullptr, service.local ? nullptr : &instances));
    }
    builder.Finish(mw_someip_config::CreateRootDirect(builder, &service_types));
    auto const* const data = reinterpret_cast<std::byte const*>(builder.GetBufferPointer());
    return {data, data + builder.GetSize()};
}

/// \brief vsomeip allows one application of a name per process. It is never started.
Routing& routing() {
    static auto routing = Routing::Create();
    return *routing.value();
}

class GatewaydServicesTest : public ::testing::Test {
   protected:
    socom::Runtime::Uptr runtime_ = socom::create_runtime();
    std::vector<Service_configs> bridged_{};
    gateway_ipc_binding::mw_com::Add_bridged_services const add_bridged_services_ =
        [this](Service_configs const& services) {
            bridged_.push_back(services);
            return true;
        };
};

TEST_F(GatewaydServicesTest, RejectsBytesThatAreNoGatewayConfig) {
    GatewaydServices services{routing(), *runtime_};
    std::vector<std::byte> const garbage{std::byte{1}, std::byte{2}, std::byte{3}};

    EXPECT_FALSE(services.Add({garbage.data(), garbage.size()}, add_bridged_services_));
    EXPECT_TRUE(bridged_.empty());
}

TEST_F(GatewaydServicesTest, RejectsAServiceTypeWithoutName) {
    GatewaydServices services{routing(), *runtime_};
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<mw_someip_config::Event>> events{
        mw_someip_config::CreateEventDirect(builder, 1U, "event_a")};
    std::vector<flatbuffers::Offset<mw_someip_config::ServiceInstance>> instances{
        mw_someip_config::CreateServiceInstanceDirect(builder, "test/instance", 1U)};
    std::vector<flatbuffers::Offset<mw_someip_config::ServiceType>> service_types{
        mw_someip_config::CreateServiceTypeDirect(builder, nullptr, 0x1051U, 1U, 0U, &events,
                                                  nullptr, &instances)};
    builder.Finish(mw_someip_config::CreateRootDirect(builder, &service_types));
    auto const* const config = reinterpret_cast<std::byte const*>(builder.GetBufferPointer());

    EXPECT_FALSE(services.Add({config, builder.GetSize()}, add_bridged_services_));
    EXPECT_TRUE(bridged_.empty());
}

TEST_F(GatewaydServicesTest, RejectsAServiceTypeWithoutEvents) {
    GatewaydServices services{routing(), *runtime_};
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<mw_someip_config::ServiceInstance>> instances{
        mw_someip_config::CreateServiceInstanceDirect(builder, "test/instance", 1U)};
    std::vector<flatbuffers::Offset<mw_someip_config::ServiceType>> service_types{
        mw_someip_config::CreateServiceTypeDirect(builder, "no_events", 0x1052U, 1U, 0U, nullptr,
                                                  nullptr, &instances)};
    builder.Finish(mw_someip_config::CreateRootDirect(builder, &service_types));
    auto const* const config = reinterpret_cast<std::byte const*>(builder.GetBufferPointer());

    EXPECT_FALSE(services.Add({config, builder.GetSize()}, add_bridged_services_));
    EXPECT_TRUE(bridged_.empty());
}

TEST_F(GatewaydServicesTest, AddsTheServicesOfTwoGatewayds) {
    GatewaydServices services{routing(), *runtime_};
    auto const first = gateway_config({{"two_first", 0x1001U, true, 1U}});
    auto const second = gateway_config({{"two_second", 0x1002U, false, 1U}});

    EXPECT_TRUE(services.Add({first.data(), first.size()}, add_bridged_services_));
    EXPECT_TRUE(services.Add({second.data(), second.size()}, add_bridged_services_));

    ASSERT_EQ(bridged_.size(), 2U);
    ASSERT_EQ(bridged_.at(0).size(), 1U);
    EXPECT_EQ(bridged_.at(0).front().service_id, 0x1001U);
    EXPECT_EQ(bridged_.at(0).front().role, Role::consumer);
    ASSERT_EQ(bridged_.at(1).size(), 1U);
    EXPECT_EQ(bridged_.at(1).front().service_id, 0x1002U);
    EXPECT_EQ(bridged_.at(1).front().role, Role::provider);
}

TEST_F(GatewaydServicesTest, RejectsOtherContentForAnAddedService) {
    GatewaydServices services{routing(), *runtime_};
    auto const added = gateway_config({{"changed", 0x1011U, true, 1U}});
    auto const changed = gateway_config({{"changed", 0x1011U, true, 2U}});
    ASSERT_TRUE(services.Add({added.data(), added.size()}, add_bridged_services_));

    EXPECT_FALSE(services.Add({changed.data(), changed.size()}, add_bridged_services_));
    EXPECT_EQ(bridged_.size(), 1U);
}

TEST_F(GatewaydServicesTest, RejectsASecondGatewaydWithATakenService) {
    GatewaydServices services{routing(), *runtime_};
    auto const first = gateway_config({{"taken", 0x1021U, true, 1U}});
    auto const second =
        gateway_config({{"taken", 0x1021U, true, 1U}, {"not_taken", 0x1022U, false, 1U}});
    auto const third = gateway_config({{"not_taken", 0x1022U, false, 1U}});
    ASSERT_TRUE(services.Add({first.data(), first.size()}, add_bridged_services_));

    EXPECT_FALSE(services.Add({second.data(), second.size()}, add_bridged_services_));
    // The rejected call took nothing.
    EXPECT_TRUE(services.Add({third.data(), third.size()}, add_bridged_services_));
}

TEST_F(GatewaydServicesTest, AddsNothingWhenTheBridgedServicesFail) {
    GatewaydServices services{routing(), *runtime_};
    auto const config = gateway_config({{"bridge_fails", 0x1031U, true, 1U}});

    EXPECT_FALSE(
        services.Add({config.data(), config.size()}, [](Service_configs const&) { return false; }));
    EXPECT_TRUE(services.Add({config.data(), config.size()}, add_bridged_services_));
}

TEST_F(GatewaydServicesTest, AddsNothingWhenANetworkServiceFails) {
    GatewaydServices services{routing(), *runtime_};
    auto const config = gateway_config(
        {{"created_first", 0x1041U, true, 1U}, {"already_provided", 0x1042U, false, 1U}});
    socom::Method_call_credentials_callback_mock method_call;
    socom::Event_subscription_change_callback_mock event_subscription_change;
    socom::Event_request_update_callback_mock event_update_request;
    socom::Method_call_payload_allocate_callback_mock method_payload_allocate;
    constexpr std::string_view kAlready_provided{"already_provided"};
    // RemoteNetworkService can't provide it a second time.
    auto blocking = runtime_->make_server_connector(
        socom::Server_service_interface_definition{
            socom::Service_interface_identifier{kAlready_provided, {1U, 0U}},
            socom::to_num_of_methods(0U), socom::to_num_of_events(1U)},
        socom::Service_instance{kAlready_provided},
        socom::Disabled_server_connector::Callbacks{
            method_call.as_function(), event_subscription_change.as_function(),
            event_update_request.as_function(), method_payload_allocate.as_function()});
    ASSERT_TRUE(blocking.has_value());

    EXPECT_FALSE(services.Add({config.data(), config.size()}, add_bridged_services_));

    blocking.value().reset();
    // Succeeds only if the first call destroyed its LocalNetworkService.
    EXPECT_TRUE(services.Add({config.data(), config.size()}, add_bridged_services_));
}

TEST_F(GatewaydServicesTest, AddsAServiceTypeWithMultipleLocalInstances) {
    GatewaydServices services{routing(), *runtime_};
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<mw_someip_config::Event>> events{
        mw_someip_config::CreateEventDirect(builder, 1U, "event_a")};
    std::vector<flatbuffers::Offset<mw_someip_config::ServiceInstance>> instances{
        mw_someip_config::CreateServiceInstanceDirect(builder, "test/instance_1", 1U),
        mw_someip_config::CreateServiceInstanceDirect(builder, "test/instance_2", 2U)};
    std::vector<flatbuffers::Offset<mw_someip_config::ServiceType>> service_types{
        mw_someip_config::CreateServiceTypeDirect(builder, "multi_instance", 0x1061U, 1U, 0U,
                                                  &events, nullptr, &instances)};
    builder.Finish(mw_someip_config::CreateRootDirect(builder, &service_types, 1U));
    auto const* const config = reinterpret_cast<std::byte const*>(builder.GetBufferPointer());

    EXPECT_TRUE(services.Add({config, builder.GetSize()}, add_bridged_services_));
    ASSERT_EQ(bridged_.size(), 1U);
    ASSERT_EQ(bridged_.front().size(), 1U);
    EXPECT_EQ(bridged_.front().front().service_id, 0x1061U);
}

}  // namespace
}  // namespace score::someipd
