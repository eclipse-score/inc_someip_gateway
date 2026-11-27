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
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/gateway_ipc_binding/impl/sample_layout.hpp"
#include "score/gateway_ipc_binding/test/unit/ipc_binding_test_fixture.hpp"
#include "score/memory/data_type_size_info.h"
#include "score/mw/com/impl/find_service_handle.h"
#include "score/mw/com/impl/instance_identifier.h"
#include "score/mw/com/impl/instance_specifier.h"

namespace score::gateway_ipc_binding::mw_com {
namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Return;

constexpr std::size_t kZeroPayloadSize{0U};
constexpr std::size_t kOverMaxPayloadSize{kMax_payload_length + 1U};
constexpr std::size_t kZeroSampleCount{0U};
constexpr std::size_t kMinConsumerSampleCount{1U};
constexpr std::size_t kMaxValidSampleCount{kNumber_of_sample_slots};
constexpr std::size_t kOverMaxSampleCount{kNumber_of_sample_slots + 1U};
constexpr std::size_t kExpectedEvent0SampleSize{24U};
constexpr std::size_t kExpectedValidConfigCaseCount{5U};
constexpr std::size_t kExpectedInvalidConfigCaseCount{14U};
constexpr std::size_t kSingleResolvedInstanceCount{1U};
constexpr std::size_t kTwoDeployedEvents{2U};
constexpr std::uint32_t kExpectedShmMajorVersion{1U};
constexpr std::uint32_t kExpectedShmMinorVersion{0U};
constexpr std::uint8_t kExpectedShmMaxSubscribers{1U};
constexpr std::uint16_t kCustomInstanceId{0xFFFFU};
constexpr std::uint16_t kFirstArbitraryEventId{0x1234U};
constexpr std::uint16_t kSecondArbitraryEventId{0x5678U};
constexpr std::uint32_t kDummyFindHandleUid{7U};

struct ServiceConfigCase {
    std::string_view description;
    Service_config config;
};

Service_config MakeCustomConfig(std::string_view const specifier, Role const role,
                                std::vector<Event_config> events,
                                std::size_t const max_sample_count,
                                std::uint16_t const service_id = kDefaultServiceId,
                                std::uint16_t const instance_id = kDefaultInstanceId) {
    auto cfg = MakeMultiEventServiceConfig(role, std::move(events), specifier, service_id,
                                           max_sample_count);
    cfg.instance_id = instance_id;
    return cfg;
}

Service_config MakeSingleEventConfig(std::string_view const specifier, Role const role,
                                     std::size_t const max_sample_count,
                                     std::size_t const max_payload_size = kEvent0MaxPayload,
                                     std::uint16_t const service_id = kDefaultServiceId,
                                     std::uint16_t const instance_id = kDefaultInstanceId) {
    return MakeCustomConfig(
        specifier, role,
        {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, max_payload_size}},
        max_sample_count, service_id, instance_id);
}

Service_config MakeTwoEventConsumerConfig(Event_config first_event, Event_config second_event) {
    return MakeCustomConfig(kValidBridgedSpecifier, Role::consumer,
                            {std::move(first_event), std::move(second_event)},
                            kMinConsumerSampleCount);
}

void AssertLolaShmTwoEventDeployment(score::mw::com::impl::InstanceIdentifier const& resolved_id,
                                     std::string const& expected_specifier,
                                     std::uint16_t const expected_service_id,
                                     std::uint16_t const expected_instance_id) {
    score::mw::com::impl::InstanceIdentifierView const view{resolved_id};
    auto const& instance_dep = view.GetServiceInstanceDeployment();
    EXPECT_EQ(instance_dep.asilLevel_, score::mw::com::impl::QualityType::kASIL_QM);
    EXPECT_EQ(instance_dep.instance_specifier_.ToString(), expected_specifier);
    EXPECT_EQ(instance_dep.service_,
              score::mw::com::impl::make_ServiceIdentifierType(
                  expected_specifier, kExpectedShmMajorVersion, kExpectedShmMinorVersion));

    auto const* const lola_inst_dep =
        std::get_if<score::mw::com::impl::LolaServiceInstanceDeployment>(
            &instance_dep.bindingInfo_);
    ASSERT_NE(lola_inst_dep, nullptr);
    ASSERT_TRUE(lola_inst_dep->instance_id_.has_value());
    EXPECT_EQ(lola_inst_dep->instance_id_->GetId(), expected_instance_id);
    ASSERT_EQ(lola_inst_dep->events_.size(), kTwoDeployedEvents);
    ASSERT_NE(lola_inst_dep->events_.find(std::string{kEvent0}), lola_inst_dep->events_.end());
    ASSERT_NE(lola_inst_dep->events_.find(std::string{kEvent1}), lola_inst_dep->events_.end());
    EXPECT_EQ(lola_inst_dep->events_.at(std::string{kEvent0}).GetNumberOfSampleSlots(),
              kNumber_of_sample_slots);
    EXPECT_EQ(lola_inst_dep->events_.at(std::string{kEvent0}).max_subscribers_,
              kExpectedShmMaxSubscribers);
    EXPECT_EQ(lola_inst_dep->events_.at(std::string{kEvent1}).GetNumberOfSampleSlots(),
              kNumber_of_sample_slots);
    EXPECT_EQ(lola_inst_dep->events_.at(std::string{kEvent1}).max_subscribers_,
              kExpectedShmMaxSubscribers);

    auto const& type_dep = view.GetServiceTypeDeployment();
    auto const* const lola_type_dep =
        std::get_if<score::mw::com::impl::LolaServiceTypeDeployment>(&type_dep.binding_info_);
    ASSERT_NE(lola_type_dep, nullptr);
    EXPECT_EQ(lola_type_dep->service_id_, expected_service_id);
    ASSERT_EQ(lola_type_dep->events_.size(), kTwoDeployedEvents);
    EXPECT_EQ(lola_type_dep->events_.at(std::string{kEvent0}), kFirstArbitraryEventId);
    EXPECT_EQ(lola_type_dep->events_.at(std::string{kEvent1}), kSecondArbitraryEventId);
}

using ServiceBindingTest = IpcBindingTestFixture;

TEST_F(ServiceBindingTest, AcceptsValidServiceConfigBoundaries) {
    std::vector<ServiceConfigCase> const valid_cases{
        {
            "consumer_min_sample_count_and_zero_payload",
            MakeSingleEventConfig(kValidBridgedSpecifier, Role::consumer, kMinConsumerSampleCount,
                                  kZeroPayloadSize),
        },
        {
            "consumer_max_sample_count_and_max_payload_length",
            MakeSingleEventConfig(kValidBridgedSpecifier, Role::consumer, kMaxValidSampleCount,
                                  kMax_payload_length),
        },
        {
            "provider_zero_sample_count_allowed",
            MakeSingleEventConfig(kValidBridgedSpecifier, Role::provider, kZeroSampleCount),
        },
        {
            "provider_max_sample_count_and_multiple_distinct_events",
            MakeCustomConfig("bridged_x", Role::provider,
                             {
                                 Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id},
                                              kZeroPayloadSize},
                                 Event_config{std::string{kEvent1}, Mw_com_event_id{kEvent1Id},
                                              kMax_payload_length},
                             },
                             kMaxValidSampleCount),
        },
        {
            "exact_eight_byte_bridged_prefix_passes_validation",
            MakeSingleEventConfig("bridged_", Role::consumer, kMinConsumerSampleCount),
        },
    };

    ASSERT_EQ(valid_cases.size(), kExpectedValidConfigCaseCount);
    for (auto const& test_case : valid_cases) {
        SCOPED_TRACE(test_case.description);
        auto const result = validate(test_case.config);
        EXPECT_TRUE(result.has_value());
    }
}

TEST_F(ServiceBindingTest, RejectsInvalidServiceConfigBoundaries) {
    Event_config const valid_ev0{std::string{kEvent0}, Mw_com_event_id{kEvent0Id},
                                 kEvent0MaxPayload};
    std::vector<ServiceConfigCase> const invalid_cases{
        {"empty_instance_specifier",
         MakeSingleEventConfig("", Role::consumer, kMinConsumerSampleCount)},
        {"truncated_seven_byte_bridged_prefix",
         MakeSingleEventConfig("bridged", Role::consumer, kMinConsumerSampleCount)},
        {"eight_byte_char_mismatch_bridge_prefix",
         MakeSingleEventConfig("bridge_x", Role::consumer, kMinConsumerSampleCount)},
        {"case_mismatched_bridged_prefix",
         MakeSingleEventConfig("Bridged_x", Role::consumer, kMinConsumerSampleCount)},
        {"non_zero_offset_before_bridged_prefix",
         MakeSingleEventConfig("_bridged_x", Role::consumer, kMinConsumerSampleCount)},
        {"empty_events_vector",
         MakeCustomConfig(kValidBridgedSpecifier, Role::consumer, {}, kMinConsumerSampleCount)},
        {"empty_event_name_on_second_event",
         MakeTwoEventConsumerConfig(
             valid_ev0, Event_config{"", Mw_com_event_id{kEvent1Id}, kEvent1MaxPayload})},
        {"duplicate_event_name_with_distinct_ids",
         MakeTwoEventConsumerConfig(
             valid_ev0,
             Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent1Id}, kEvent1MaxPayload})},
        {"duplicate_event_id_with_distinct_names",
         MakeTwoEventConsumerConfig(
             valid_ev0,
             Event_config{std::string{kEvent1}, Mw_com_event_id{kEvent0Id}, kEvent1MaxPayload})},
        {"payload_size_one_above_uint32_max",
         MakeTwoEventConsumerConfig(
             valid_ev0,
             Event_config{std::string{kEvent1}, Mw_com_event_id{kEvent1Id}, kOverMaxPayloadSize})},
        {"payload_size_at_size_max",
         MakeSingleEventConfig(kValidBridgedSpecifier, Role::provider, kZeroSampleCount,
                               std::numeric_limits<std::size_t>::max())},
        {"consumer_zero_sample_count_rejected",
         MakeSingleEventConfig(kValidBridgedSpecifier, Role::consumer, kZeroSampleCount)},
        {"consumer_sample_count_above_slot_limit",
         MakeSingleEventConfig(kValidBridgedSpecifier, Role::consumer, kOverMaxSampleCount)},
        {"provider_sample_count_above_slot_limit",
         MakeSingleEventConfig(kValidBridgedSpecifier, Role::provider, kOverMaxSampleCount)},
    };

    ASSERT_EQ(invalid_cases.size(), kExpectedInvalidConfigCaseCount);
    auto const expected_error =
        MakeError(Mw_com_binding_error::logic_error_invalid_service_configuration);
    for (auto const& test_case : invalid_cases) {
        SCOPED_TRACE(test_case.description);
        auto const result = validate(test_case.config);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), expected_error);
    }
}

TEST_F(ServiceBindingTest, ParsesAndVerifiesDeployedInstanceSpecifiers) {
    StubInstanceNotResolved(kSecondBridgedSpecifier);

    auto const valid_spec = make_instance_specifier(std::string{kValidBridgedSpecifier});
    ASSERT_TRUE(valid_spec.has_value());
    EXPECT_EQ(
        *valid_spec,
        score::mw::com::InstanceSpecifier::Create(std::string{kValidBridgedSpecifier}).value());

    auto const expected_spec_error =
        MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier);

    auto const empty_spec = make_instance_specifier("");
    ASSERT_FALSE(empty_spec.has_value());
    EXPECT_EQ(empty_spec.error(), expected_spec_error);

    auto const malformed_spec = make_instance_specifier(std::string{kInvalidBridgedSpecifier});
    ASSERT_FALSE(malformed_spec.has_value());
    EXPECT_EQ(malformed_spec.error(), expected_spec_error);

    EXPECT_TRUE(
        check_instance_is_deployed(*valid_spec, std::string{kValidBridgedSpecifier}).has_value());

    auto const undeployed_spec = make_instance_specifier(std::string{kSecondBridgedSpecifier});
    ASSERT_TRUE(undeployed_spec.has_value());
    auto const undeployed_check =
        check_instance_is_deployed(*undeployed_spec, std::string{kSecondBridgedSpecifier});
    ASSERT_FALSE(undeployed_check.has_value());
    EXPECT_EQ(undeployed_check.error(), expected_spec_error);
}

TEST_F(ServiceBindingTest, AddsUndeployedServicesToRuntimeAndSkipsDeployedOnes) {
    EXPECT_TRUE(add_to_mw_com_runtime({}).has_value());

    auto const first_ids = NextUniqueBridgedIds("runtime_first");
    auto const second_ids = NextUniqueBridgedIds("runtime_second");
    ASSERT_TRUE(ResolveInRealRuntime(first_ids.specifier).empty());
    ASSERT_TRUE(ResolveInRealRuntime(second_ids.specifier).empty());

    auto const first_cfg = MakeCustomConfig(
        first_ids.specifier, Role::consumer,
        {
            Event_config{std::string{kEvent0}, Mw_com_event_id{kFirstArbitraryEventId},
                         kEvent0MaxPayload},
            Event_config{std::string{kEvent1}, Mw_com_event_id{kSecondArbitraryEventId},
                         kEvent1MaxPayload},
        },
        kDefaultMaxSampleCount, first_ids.service_id, kCustomInstanceId);
    auto const second_cfg = MakeCustomConfig(
        second_ids.specifier, Role::provider,
        {Event_config{std::string{kEvent2}, Mw_com_event_id{kEvent2Id}, kEvent2MaxPayload}},
        kZeroSampleCount, second_ids.service_id, kDefaultInstanceId);

    ASSERT_TRUE(add_to_mw_com_runtime({first_cfg, second_cfg}).has_value());

    auto const first_resolved = ResolveInRealRuntime(first_ids.specifier);
    ASSERT_EQ(first_resolved.size(), kSingleResolvedInstanceCount);
    ASSERT_NO_FATAL_FAILURE(AssertLolaShmTwoEventDeployment(
        first_resolved.front(), first_ids.specifier, first_ids.service_id, kCustomInstanceId));

    ASSERT_EQ(ResolveInRealRuntime(second_ids.specifier).size(), kSingleResolvedInstanceCount);

    auto const first_spec = make_instance_specifier(first_ids.specifier);
    ASSERT_TRUE(first_spec.has_value());
    EXPECT_TRUE(check_instance_is_deployed(*first_spec, first_ids.specifier).has_value());

    // Idempotent repeat call + mix of already-deployed and newly undeployed services skips
    // already-deployed services without calling AddConfiguration twice for them.
    auto const third_ids = NextUniqueBridgedIds("runtime_third");
    ASSERT_TRUE(ResolveInRealRuntime(third_ids.specifier).empty());
    auto const third_cfg =
        MakeSingleEventConfig(third_ids.specifier, Role::consumer, kDefaultMaxSampleCount,
                              kEvent0MaxPayload, third_ids.service_id);
    auto const pre_stubbed_cfg = MakeServiceConfig(Role::consumer, kValidBridgedSpecifier);

    ASSERT_TRUE(
        add_to_mw_com_runtime({pre_stubbed_cfg, first_cfg, second_cfg, third_cfg}).has_value());
    EXPECT_EQ(ResolveInRealRuntime(third_ids.specifier).size(), kSingleResolvedInstanceCount);
}

TEST_F(ServiceBindingTest, RejectsInvalidOrDuplicateBatchWithoutMutatingRuntime) {
    auto const invalid_cfg_error =
        MakeError(Mw_com_binding_error::logic_error_invalid_service_configuration);
    auto const invalid_spec_error =
        MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier);

    auto const shared_ids = NextUniqueBridgedIds("dup_spec");
    auto const dup_spec_res = add_to_mw_com_runtime({
        MakeSingleEventConfig(shared_ids.specifier, Role::consumer, kDefaultMaxSampleCount,
                              kEvent0MaxPayload, shared_ids.service_id),
        MakeSingleEventConfig(shared_ids.specifier, Role::provider, kZeroSampleCount,
                              kEvent0MaxPayload, NextUniqueBridgedIds("dup_spec_b").service_id),
    });
    ASSERT_FALSE(dup_spec_res.has_value());
    EXPECT_EQ(dup_spec_res.error(), invalid_cfg_error);
    EXPECT_TRUE(ResolveInRealRuntime(shared_ids.specifier).empty());

    auto const ids_a = NextUniqueBridgedIds("dup_id_a");
    auto const ids_b = NextUniqueBridgedIds("dup_id_b");
    auto const dup_id_res = add_to_mw_com_runtime({
        MakeSingleEventConfig(ids_a.specifier, Role::consumer, kDefaultMaxSampleCount,
                              kEvent0MaxPayload, ids_a.service_id),
        MakeSingleEventConfig(ids_b.specifier, Role::provider, kZeroSampleCount, kEvent0MaxPayload,
                              ids_a.service_id),
    });
    ASSERT_FALSE(dup_id_res.has_value());
    EXPECT_EQ(dup_id_res.error(), invalid_cfg_error);
    EXPECT_TRUE(ResolveInRealRuntime(ids_a.specifier).empty());
    EXPECT_TRUE(ResolveInRealRuntime(ids_b.specifier).empty());

    auto const valid_before_bad_cfg = NextUniqueBridgedIds("valid_before_invalid_cfg");
    auto const bad_cfg_res = add_to_mw_com_runtime({
        MakeSingleEventConfig(valid_before_bad_cfg.specifier, Role::consumer,
                              kDefaultMaxSampleCount, kEvent0MaxPayload,
                              valid_before_bad_cfg.service_id),
        MakeSingleEventConfig("unprefixed/spec", Role::consumer, kDefaultMaxSampleCount,
                              kEvent0MaxPayload, NextUniqueBridgedIds("bad_cfg").service_id),
    });
    ASSERT_FALSE(bad_cfg_res.has_value());
    EXPECT_EQ(bad_cfg_res.error(), invalid_cfg_error);
    EXPECT_TRUE(ResolveInRealRuntime(valid_before_bad_cfg.specifier).empty());

    auto const valid_before_bad_spec = NextUniqueBridgedIds("valid_before_bad_spec");
    auto const bad_spec_res = add_to_mw_com_runtime({
        MakeSingleEventConfig(valid_before_bad_spec.specifier, Role::consumer,
                              kDefaultMaxSampleCount, kEvent0MaxPayload,
                              valid_before_bad_spec.service_id),
        MakeSingleEventConfig(kInvalidBridgedSpecifier, Role::consumer, kDefaultMaxSampleCount,
                              kEvent0MaxPayload, NextUniqueBridgedIds("malformed_spec").service_id),
    });
    ASSERT_FALSE(bad_spec_res.has_value());
    EXPECT_EQ(bad_spec_res.error(), invalid_spec_error);
    EXPECT_TRUE(ResolveInRealRuntime(valid_before_bad_spec.specifier).empty());
}

TEST_F(ServiceBindingTest, RejectsInvalidOrUndeployedConfigInMakeServiceBinding) {
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .Times(0);
    StubInstanceNotResolved(kSecondBridgedSpecifier);
    ExpectServerConnectorCreationFailure();
    ExpectSkeletonCreationFailure();

    auto const invalid_cfg = MakeServiceConfig(Role::consumer, "missing_prefix/service");
    auto const invalid_res = make_service_binding(socom_runtime_, invalid_cfg);
    ASSERT_FALSE(invalid_res.has_value());
    EXPECT_EQ(invalid_res.error(),
              MakeError(Mw_com_binding_error::logic_error_invalid_service_configuration));

    auto undeployed_cfg = MakeServiceConfig(Role::consumer, kSecondBridgedSpecifier);
    undeployed_cfg.service_id = kSecondServiceId;
    auto const undeployed_res = make_service_binding(socom_runtime_, undeployed_cfg);
    ASSERT_FALSE(undeployed_res.has_value());
    EXPECT_EQ(undeployed_res.error(),
              MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier));

    auto const setup_fail_res =
        make_service_binding(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_FALSE(setup_fail_res.has_value());
    EXPECT_EQ(setup_fail_res.error(),
              MakeError(Mw_com_binding_error::runtime_error_service_setup_failed));

    auto const provider_fail_res =
        make_service_binding(socom_runtime_, MakeServiceConfig(Role::provider));
    ASSERT_FALSE(provider_fail_res.has_value());
    EXPECT_EQ(provider_fail_res.error(),
              MakeError(Mw_com_binding_error::runtime_error_service_setup_failed));
}

TEST_F(ServiceBindingTest, CreatesConsumerServiceBindingAndDefersDiscoveryUntilStart) {
    auto* const server_connector = ExpectServerConnectorCreated();
    EXPECT_CALL(*server_connector, enable()).Times(0);

    auto const expected_specifier =
        score::mw::com::impl::InstanceSpecifier::Create(std::string{kValidBridgedSpecifier})
            .value();
    auto const expected_find_handle =
        score::mw::com::impl::make_FindServiceHandle(kDummyFindHandleUid);

    std::size_t start_find_calls{0U};
    EXPECT_CALL(service_discovery_mock(), StartFindService(_, expected_specifier))
        .WillOnce([&start_find_calls, expected_find_handle](auto, auto) {
            ++start_find_calls;
            return score::Result<score::mw::com::impl::FindServiceHandle>{expected_find_handle};
        });
    EXPECT_CALL(service_discovery_mock(), StopFindService(expected_find_handle))
        .WillOnce(Return(score::Result<void>{}));

    auto consumer_res = make_service_binding(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_TRUE(consumer_res.has_value());
    ASSERT_NE(*consumer_res, nullptr);
    EXPECT_EQ(start_find_calls, 0U);

    EXPECT_TRUE((*consumer_res)->start().has_value());
    EXPECT_EQ(start_find_calls, 1U);
}

TEST_F(ServiceBindingTest, CreatesProviderServiceBindingWithNoOpStart) {
    auto skel_mocks = ExpectSkeletonWithEvent(kEvent0);
    ON_CALL(*skel_mocks.event, GetEventDataTypeSizeInfo())
        .WillByDefault(
            Return(score::memory::DataTypeSizeInfo{kExpectedEvent0SampleSize, kSample_alignment}));
    (void)ExpectClientConnectorCreated();
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .Times(0);

    auto provider_cfg = MakeServiceConfig(Role::provider);
    provider_cfg.max_sample_count = kZeroSampleCount;
    auto provider_res = make_service_binding(socom_runtime_, provider_cfg);
    ASSERT_TRUE(provider_res.has_value());
    ASSERT_NE(*provider_res, nullptr);
    EXPECT_TRUE((*provider_res)->start().has_value());
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
