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

#include "score/gateway_ipc_binding/impl/service_binding.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/gateway_ipc_binding/impl/sample_layout.hpp"
#include "score/gateway_ipc_binding/test/unit/ipc_binding_test_fixture.hpp"
#include "score/socom/client_connector.hpp"
#include "score/socom/client_connector_mock.hpp"
#include "score/socom/runtime_mock.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/server_connector_mock.hpp"

namespace score::gateway_ipc_binding::mw_com {
namespace {

using ::testing::_;
using ::testing::ByMove;
using ::testing::NiceMock;
using ::testing::Return;

constexpr std::string_view kMissingPrefixSpecifier{"not_bridged/service"};
constexpr std::string_view kInvalidPrefixedSpecifier{"bridged_//invalid_spec"};
constexpr std::string_view kSecondBridgedSpecifier{"bridged_ipc/second"};
constexpr std::string_view kSecondEventName{"ev1"};
constexpr std::uint16_t kSecondEventId{2U};
constexpr std::size_t kSecondEventMaxPayload{8U};
constexpr std::size_t kZeroMaxSampleCount{0U};

class ServiceBindingTest : public IpcBindingTestFixture {
   protected:
    void ExpectProviderSkeletonCreated(Service_config const& cfg) {
        auto skel_mock = std::make_unique<NiceMock<testing::Skeleton>>();
        auto skel_event_mock = std::make_unique<NiceMock<testing::GenericSkeletonEvent>>();
        ON_CALL(*skel_event_mock, GetEventDataTypeSizeInfo())
            .WillByDefault(Return(score::memory::DataTypeSizeInfo{sample_size(cfg.events.front()),
                                                                  kSample_alignment}));
        ON_CALL(*skel_event_mock, SetReceiveHandlerRegistrationChangedHandler(_))
            .WillByDefault(Return(score::Result<void>{}));
        ON_CALL(*skel_event_mock, UnsetReceiveHandlerRegistrationChangedHandler())
            .WillByDefault(Return(score::Result<void>{}));
        EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
            .WillOnce(Return(ByMove(std::move(skel_mock))));
        EXPECT_CALL(generic_skeleton_event_factory_mock(), Create(_, _, kEvent0, _))
            .WillOnce(Return(ByMove(std::move(skel_event_mock))));
    }
};

// Preserves the exact instance specifier string when parsing succeeds.
TEST_F(ServiceBindingTest, MakeInstanceSpecifierAcceptsValidSpecifier) {
    auto const valid = make_instance_specifier(std::string{kValidBridgedSpecifier});
    ASSERT_TRUE(valid.has_value());
    EXPECT_EQ(valid->ToString(), kValidBridgedSpecifier);
}

// Invalid specifier syntax returns logic_error_invalid_instance_specifier, not a generic setup
// error.
TEST_F(ServiceBindingTest, MakeInstanceSpecifierRejectsInvalidSpecifier) {
    auto const invalid = make_instance_specifier(std::string{kInvalidSpecifier});
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error(), Mw_com_binding_error::logic_error_invalid_instance_specifier);
}

// Fails immediately when the specifier is missing from mw_com_config.json instead of letting LoLa
// StartFindService() wait indefinitely.
TEST_F(ServiceBindingTest, CheckInstanceIsDeployedRejectsUndeployedSpecifier) {
    auto const valid = make_instance_specifier(std::string{kValidBridgedSpecifier});
    ASSERT_TRUE(valid.has_value());

    StubInstanceNotResolved();
    auto const not_deployed =
        check_instance_is_deployed(*valid, std::string{kValidBridgedSpecifier});
    ASSERT_FALSE(not_deployed.has_value());
    EXPECT_EQ(not_deployed.error(), Mw_com_binding_error::logic_error_invalid_instance_specifier);
}

// Succeeds when ResolveInstanceIDs() returns at least one InstanceIdentifier.
TEST_F(ServiceBindingTest, CheckInstanceIsDeployedAcceptsDeployedSpecifier) {
    auto const valid = make_instance_specifier(std::string{kValidBridgedSpecifier});
    ASSERT_TRUE(valid.has_value());

    auto const deployed = check_instance_is_deployed(*valid, std::string{kValidBridgedSpecifier});
    EXPECT_TRUE(deployed.has_value());
}

// Accepts a complete Service_config that satisfies all identifier, event, and sample limits.
TEST_F(ServiceBindingTest, ValidateAcceptsValidServiceConfig) {
    auto const valid_cfg = MakeServiceConfig();
    EXPECT_TRUE(validate(valid_cfg).has_value());
}

// Requires the "bridged_" prefix so gateway specifiers remain distinct from application specifiers
// in mw::com.
TEST_F(ServiceBindingTest, ValidateRejectsMissingBridgedPrefix) {
    auto const no_prefix = MakeServiceConfig(Role::consumer, kMissingPrefixSpecifier);
    auto const res = validate(no_prefix);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Rejects a service with zero events because GenericProxy and GenericSkeleton only forward events,
// not methods.
TEST_F(ServiceBindingTest, ValidateRejectsEmptyEvents) {
    auto no_events = MakeServiceConfig();
    no_events.events.clear();
    auto const res = validate(no_events);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Rejects an empty event name before LoLa's JSON configuration parser aborts on "".
TEST_F(ServiceBindingTest, ValidateRejectsEmptyEventName) {
    auto empty_name = MakeServiceConfig();
    empty_name.events.front().name.clear();
    auto const res = validate(empty_name);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Rejects two events with the same LoLa eventName before AddConfiguration() aborts.
TEST_F(ServiceBindingTest, ValidateRejectsDuplicateEventName) {
    auto dup_name = MakeServiceConfig();
    dup_name.events.push_back(Event_config{std::string{kEvent0}, Mw_com_event_id{kSecondEventId},
                                           kSecondEventMaxPayload});
    auto const res = validate(dup_name);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Rejects two events with the same LoLa eventId before AddConfiguration() aborts.
TEST_F(ServiceBindingTest, ValidateRejectsDuplicateEventId) {
    auto dup_id = MakeServiceConfig();
    dup_id.events.push_back(Event_config{std::string{kSecondEventName}, Mw_com_event_id{kEvent0Id},
                                         kSecondEventMaxPayload});
    auto const res = validate(dup_id);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Rejects max_payload_size > UINT32_MAX because the SHM sample header stores length in 4 bytes.
TEST_F(ServiceBindingTest, ValidateRejectsOversizedEventPayload) {
    auto oversized = MakeServiceConfig();
    oversized.events.front().max_payload_size = kMax_payload_length + 1U;
    auto const res = validate(oversized);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Rejects max_sample_count == 0 on a consumer because LoLa Subscribe(0) succeeds without error and
// delivers no events.
TEST_F(ServiceBindingTest, ValidateRejectsConsumerWithZeroMaxSampleCount) {
    auto zero_samples = MakeServiceConfig(Role::consumer);
    zero_samples.max_sample_count = kZeroMaxSampleCount;
    auto const res = validate(zero_samples);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Rejects max_sample_count > kNumber_of_sample_slots so Subscribe() will not fail for lack of SHM
// sample slots.
TEST_F(ServiceBindingTest, ValidateRejectsExcessiveMaxSampleCount) {
    auto too_many_samples = MakeServiceConfig(Role::provider);
    too_many_samples.max_sample_count = kNumber_of_sample_slots + 1U;
    auto const res = validate(too_many_samples);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Runs validate() in add_to_mw_com_runtime() before passing generated JSON to LoLa
// AddConfiguration().
TEST_F(ServiceBindingTest, AddToMwComRuntimeRejectsInvalidServiceConfig) {
    auto no_events = MakeServiceConfig();
    no_events.events.clear();
    auto const res = add_to_mw_com_runtime({no_events});
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Rejects duplicate instance_specifier values across services before LoLa AddConfiguration()
// aborts.
TEST_F(ServiceBindingTest, AddToMwComRuntimeRejectsDuplicateInstanceSpecifier) {
    auto const valid_cfg = MakeServiceConfig();
    auto const res = add_to_mw_com_runtime({valid_cfg, valid_cfg});
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Rejects two services configured with the same LoLa service_id.
TEST_F(ServiceBindingTest, AddToMwComRuntimeRejectsDuplicateServiceId) {
    auto const valid_cfg = MakeServiceConfig();
    auto second_cfg = MakeServiceConfig(Role::consumer, kSecondBridgedSpecifier);
    second_cfg.service_id = valid_cfg.service_id;
    auto const res = add_to_mw_com_runtime({valid_cfg, second_cfg});
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Rejects a "bridged_" specifier whose remaining path syntax fails InstanceSpecifier::Create().
TEST_F(ServiceBindingTest, AddToMwComRuntimeRejectsMalformedBridgedInstanceSpecifier) {
    auto const bad_syntax = MakeServiceConfig(Role::consumer, kInvalidPrefixedSpecifier);
    auto const res = add_to_mw_com_runtime({bad_syntax});
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_instance_specifier);
}

// Skips AddConfiguration() when the service is already in mw_com_config.json because re-adding a
// deployed service aborts LoLa.
TEST_F(ServiceBindingTest, AddToMwComRuntimeSucceedsWhenServiceAlreadyDeployed) {
    auto const valid_cfg = MakeServiceConfig();
    EXPECT_TRUE(add_to_mw_com_runtime({valid_cfg}).has_value());
}

// Runs validate() in make_service_binding() even when static mw_com_config.json was used without
// calling add_to_mw_com_runtime().
TEST_F(ServiceBindingTest, MakeServiceBindingRejectsInvalidServiceConfig) {
    auto no_events = MakeServiceConfig();
    no_events.events.clear();
    auto const res = make_service_binding(socom_runtime_, no_events);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Rejects an undeployed consumer specifier before creating a SOCom Server_connector.
TEST_F(ServiceBindingTest, MakeServiceBindingRejectsUndeployedConsumer) {
    StubInstanceNotResolved();
    auto const res = make_service_binding(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::logic_error_invalid_instance_specifier);
}

// Rejects an undeployed provider specifier before creating a SOCom Client_connector.
TEST_F(ServiceBindingTest, MakeServiceBindingRejectsUndeployedProvider) {
    StubInstanceNotResolved();
    auto const res = make_service_binding(socom_runtime_, MakeServiceConfig(Role::provider));
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Mw_com_binding_error::runtime_error_service_setup_failed);
}

// Role is named from mw::com's perspective, so Role::consumer creates a SOCom Server_connector.
TEST_F(ServiceBindingTest, MakeServiceBindingCreatesConsumerBinding) {
    EXPECT_CALL(socom_runtime_, make_server_connector(_, _, _))
        .WillOnce(Return(ByMove(score::Result<socom::Disabled_server_connector::Uptr>{
            std::make_unique<NiceMock<socom::Server_connector_mock>>()})));

    auto const res = make_service_binding(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_TRUE(res.has_value());
    EXPECT_NE(*res, nullptr);
}

// Role::provider creates a LoLa GenericSkeleton and a SOCom Client_connector.
TEST_F(ServiceBindingTest, MakeServiceBindingCreatesProviderBinding) {
    auto const cfg = MakeServiceConfig(Role::provider);
    ExpectProviderSkeletonCreated(cfg);
    EXPECT_CALL(socom_runtime_, make_client_connector(_, _, _))
        .WillOnce(Return(ByMove(score::Result<socom::Client_connector::Uptr>{
            std::make_unique<NiceMock<socom::Client_connector_mock>>()})));

    auto const res = make_service_binding(socom_runtime_, cfg);
    ASSERT_TRUE(res.has_value());
    EXPECT_NE(*res, nullptr);
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
