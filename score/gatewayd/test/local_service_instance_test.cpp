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

#include "score/gatewayd/impl/local_service_instance.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "flatbuffers/flatbuffers.h"
#include "score/config/mw_someip_config_generated.h"
#include "score/gatewayd/test/service_instance_test_fixture.h"
#include "score/mw/com/com_error_domain.h"
#include "score/mw/com/impl/find_service_handle.h"
#include "score/mw/com/impl/find_service_handler.h"
#include "score/mw/com/impl/handle_type.h"
#include "score/mw/com/impl/instance_identifier.h"
#include "score/mw/com/impl/instance_specifier.h"
#include "score/mw/com/impl/scoped_event_receive_handler.h"
#include "score/mw/com/types.h"
#include "score/serializer/pre_serialized_data.h"
#include "score/socom/error.hpp"
#include "score/socom/event.hpp"
#include "score/socom/payload.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/service_interface_definition.hpp"
#include "score/socom/service_interface_identifier.hpp"
#include "score/someip/constants.h"
#include "score/test/mw_com_mocks.hpp"

namespace score::someip_gateway::gatewayd {
namespace {

using ::testing::_;
using ::testing::An;
using ::testing::ElementsAreArray;
using ::testing::Return;

constexpr std::uint16_t kCanonicalServiceId{0x8001U};
constexpr std::uint16_t kCanonicalEventId{0xFF7FU};
constexpr std::uint8_t kCanonicalMajorVersion{0x80U};
constexpr std::uint32_t kCanonicalMinorVersion{0U};

constexpr std::string_view kCustomServiceName{"radar_front"};
constexpr std::uint16_t kCustomServiceId{0x4321U};
constexpr std::uint8_t kCustomMajorVersion{3U};
constexpr std::uint32_t kCustomMinorVersion{7U};
constexpr std::uint16_t kExpectedMinorVersionU16{7U};

constexpr std::uint16_t kZeroMethods{0U};
constexpr std::uint16_t kZeroEvents{0U};
constexpr std::uint16_t kTwoEvents{2U};
constexpr std::size_t kThreeProxyEvents{3U};
constexpr std::uint16_t kFourEvents{4U};

constexpr socom::Event_id kPositionalEventId0{0U};
constexpr socom::Event_id kPositionalEventId3{3U};
constexpr socom::Method_id kSampleMethodId{0U};

constexpr std::size_t kZeroBytes{0U};
constexpr std::size_t kOneByte{1U};
constexpr std::size_t kThreeSampleBatchCount{3U};
constexpr std::size_t kUndersizedFifteenBytes{15U};
constexpr std::size_t kHeaderOnlySixteenBytes{score::someip::kSomeipFullHeaderSize};
constexpr std::size_t kEighteenByteFrameSize{18U};
constexpr std::size_t kOffByOneNineteenBytes{19U};
constexpr std::size_t kExactTwentyBytes{20U};
constexpr std::size_t kStandardPayloadBufferCapacity{32U};
constexpr std::uint32_t kMultiByteLengthMaxMessageSize{300U};
constexpr std::size_t kMultiBytePayloadSize{250U};
constexpr std::size_t kMultiByteFrameCapacity{280U};
constexpr std::size_t kMultiByteExpectedFrameSize{266U};

constexpr std::size_t kExpectedSingleCall{1U};
constexpr std::size_t kExpectedTwoFrames{2U};
constexpr std::size_t kExpectedSingleInstance{1U};
constexpr std::size_t kExpectedZeroInstances{0U};
constexpr std::size_t kFindHandleUid{42U};
constexpr std::uint16_t kPrimaryInstanceId{1U};
constexpr std::uint16_t kSecondaryInstanceId{2U};
constexpr std::size_t kConcurrentWorkerCount{4U};
constexpr std::size_t kUndersizedCaseCount{4U};
constexpr std::size_t kMalformedSpecifierCount{4U};
constexpr std::size_t kInvalidCreateCaseCount{5U};
constexpr std::size_t kInvalidAsyncNullCaseCount{6U};

constexpr std::byte kPrefillSentinelByte{0xAAU};
constexpr std::byte kCanarySentinelByte{0xCCU};
constexpr std::byte kUnserializedTailSentinelByte{0xEEU};
constexpr std::uint8_t kMultiBytePayloadPattern{0x5AU};

// Pinned 20-byte SOME/IP notification frame for service_id = 0x8001, event_id = 0xFF7F,
// major_version = 0x80, and 4-byte serialized payload {0x00, 0x7F, 0x80, 0xFF} (Length = 12).
constexpr std::array<std::uint8_t, kExactTwentyBytes> kCanonicalTwentyByteWireVector{
    0x80U, 0x01U, 0xFFU, 0x7FU, 0x00U, 0x00U, 0x00U, 0x0CU, 0xFFU, 0xFFU,
    0x00U, 0x00U, 0x01U, 0x80U, 0x02U, 0x00U, 0x00U, 0x7FU, 0x80U, 0xFFU,
};

// Pinned 16-byte SOME/IP notification frame for a 0-byte serialized payload (Length = 8).
constexpr std::array<std::uint8_t, kHeaderOnlySixteenBytes> kCanonicalZeroPayloadWireVector{
    0x80U, 0x01U, 0xFFU, 0x7FU, 0x00U, 0x00U, 0x00U, 0x08U,
    0xFFU, 0xFFU, 0x00U, 0x00U, 0x01U, 0x80U, 0x02U, 0x00U,
};

// Pinned 16-byte SOME/IP header prefix for a 250-byte serialized payload (Length = 258 = 0x0102).
constexpr std::array<std::uint8_t, kHeaderOnlySixteenBytes> kMultiByteLengthHeaderPrefix{
    0x80U, 0x01U, 0xFFU, 0x7FU, 0x00U, 0x00U, 0x01U, 0x02U,
    0xFFU, 0xFFU, 0x00U, 0x00U, 0x01U, 0x80U, 0x02U, 0x00U,
};

// Pinned 20-byte frames for kServiceId = 0x1234, kEventSerializedId = 0x8001, major = 1.
constexpr std::array<std::uint8_t, kExactTwentyBytes> kSeamValidFrameOne{
    0x12U, 0x34U, 0x80U, 0x01U, 0x00U, 0x00U, 0x00U, 0x0CU, 0xFFU, 0xFFU,
    0x00U, 0x00U, 0x01U, 0x01U, 0x02U, 0x00U, 0x11U, 0x22U, 0x33U, 0x44U,
};

constexpr std::array<std::uint8_t, kExactTwentyBytes> kSeamValidFrameTwo{
    0x12U, 0x34U, 0x80U, 0x01U, 0x00U, 0x00U, 0x00U, 0x0CU, 0xFFU, 0xFFU,
    0x00U, 0x00U, 0x01U, 0x01U, 0x02U, 0x00U, 0x99U, 0xAAU, 0xBBU, 0xCCU,
};

// Pinned 18-byte frames (2-byte payloads, Length = 10) for multi-event and batch tests.
constexpr std::array<std::uint8_t, kEighteenByteFrameSize> kFirstEventWireFrame{
    0x12U, 0x34U, 0x80U, 0x01U, 0x00U, 0x00U, 0x00U, 0x0AU, 0xFFU,
    0xFFU, 0x00U, 0x00U, 0x01U, 0x01U, 0x02U, 0x00U, 0xA1U, 0xB2U,
};

constexpr std::array<std::uint8_t, kEighteenByteFrameSize> kFourthEventWireFrame{
    0x12U, 0x34U, 0x80U, 0x04U, 0x00U, 0x00U, 0x00U, 0x0AU, 0xFFU,
    0xFFU, 0x00U, 0x00U, 0x01U, 0x01U, 0x02U, 0x00U, 0xC3U, 0xD4U,
};

struct CapturedEventFrame {
    socom::Event_id event_id{0U};
    std::vector<std::uint8_t> bytes{};
};

struct CanaryBackingBuffer {
    std::vector<std::byte> storage{};

    explicit CanaryBackingBuffer(std::size_t const capacity,
                                 std::byte const fill_byte = kPrefillSentinelByte)
        : storage(capacity, fill_byte) {}

    socom::Writable_payload MakePayload(std::size_t const writable_size) noexcept {
        socom::Payload::Writable_span const span{storage.data(), writable_size};
        return socom::Writable_payload{span};
    }

    bool IsUnmodified(std::byte const expected_fill) const noexcept {
        return std::all_of(
            storage.cbegin(), storage.cend(),
            [expected_fill](std::byte const b) noexcept { return b == expected_fill; });
    }
};

template <std::size_t MaxCapacity>
serializer::PreSerializedData<MaxCapacity> MakePreSerializedSample(
    std::initializer_list<std::uint8_t> const payload_bytes,
    std::byte const tail_fill = kUnserializedTailSentinelByte) {
    serializer::PreSerializedData<MaxCapacity> sample{};
    std::fill(std::begin(sample.data), std::end(sample.data), tail_fill);
    sample.size = payload_bytes.size();
    std::size_t index{0U};
    for (std::uint8_t const value : payload_bytes) {
        if (index < MaxCapacity) {
            sample.data[index] = static_cast<std::byte>(value);
            ++index;
        }
    }
    return sample;
}

template <typename CallbackType>
void InvokeSampleCallback(CallbackType& callback, score::mw::com::impl::SamplePtr<void> sample) {
    if constexpr (std::is_invocable_v<CallbackType&, score::mw::com::impl::SamplePtr<void>>) {
        callback(std::move(sample));
    } else {
        callback(std::move(sample), {});
    }
}

std::vector<std::uint8_t> CopyPayloadBytes(socom::Payload const& payload) {
    auto const span = payload.data();
    std::vector<std::uint8_t> out{};
    out.reserve(span.size());
    for (std::byte const b : span) {
        out.push_back(std::to_integer<std::uint8_t>(b));
    }
    return out;
}

enum class MalformedConfigKind : std::uint8_t {
    kNullServiceInstance,
    kMissingInstanceSpecifierField,
    kNullServiceType,
    kMissingServiceTypeNameField,
    kMissingEventsVectorField,
    kMissingEventNameField,
};

BuiltConfig BuildRawConfigVariant(MalformedConfigKind const kind) {
    if (kind == MalformedConfigKind::kNullServiceInstance) {
        auto cfg = BuildConfig({EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize}});
        cfg.local_instance.reset();
        return cfg;
    }
    if (kind == MalformedConfigKind::kNullServiceType) {
        auto cfg = BuildConfig({EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize}});
        cfg.service_type.reset();
        return cfg;
    }

    flatbuffers::FlatBufferBuilder fbb{};

    flatbuffers::Offset<mw_someip_config::ServiceInstance> local_inst_offset{};
    if (kind == MalformedConfigKind::kMissingInstanceSpecifierField) {
        auto const inst_start = fbb.StartTable();
        fbb.AddElement<std::uint16_t>(mw_someip_config::ServiceInstance::VT_INSTANCE_ID,
                                      kInstanceId, 0U);
        local_inst_offset =
            flatbuffers::Offset<mw_someip_config::ServiceInstance>{fbb.EndTable(inst_start)};
    } else {
        local_inst_offset = mw_someip_config::CreateServiceInstance(
            fbb, fbb.CreateString(kLocalSpecifier.data(), kLocalSpecifier.size()), kInstanceId);
    }
    auto const local_instances = fbb.CreateVector(&local_inst_offset, kSingleElementCount);

    flatbuffers::Offset<mw_someip_config::Event> event_offset{};
    if (kind == MalformedConfigKind::kMissingEventNameField) {
        auto const ev_start = fbb.StartTable();
        fbb.AddElement<std::uint16_t>(mw_someip_config::Event::VT_EVENT_ID, kEventSerializedId, 0U);
        event_offset = flatbuffers::Offset<mw_someip_config::Event>{fbb.EndTable(ev_start)};
    } else {
        auto const null_cfg = mw_someip_config::CreateNullSerializerConfig(fbb, kMaxMessageSize);
        event_offset = mw_someip_config::CreateEvent(
            fbb, kEventSerializedId,
            fbb.CreateString(kEventSerialized.data(), kEventSerialized.size()),
            mw_someip_config::SerializationConfig_NullSerializerConfig, null_cfg.Union());
    }

    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<mw_someip_config::Event>>>
        events_offset{};
    if (kind != MalformedConfigKind::kMissingEventsVectorField) {
        events_offset = fbb.CreateVector(&event_offset, kSingleElementCount);
    }

    flatbuffers::Offset<flatbuffers::String> type_name_offset{};
    if (kind != MalformedConfigKind::kMissingServiceTypeNameField) {
        type_name_offset = fbb.CreateString(kServiceTypeName.data(), kServiceTypeName.size());
    }

    mw_someip_config::ServiceTypeBuilder st_builder{fbb};
    st_builder.add_service_id(kServiceId);
    st_builder.add_service_version_major(kServiceMajorVersion);
    st_builder.add_service_version_minor(kServiceMinorVersion);
    st_builder.add_local_service_instances(local_instances);
    if (kind != MalformedConfigKind::kMissingServiceTypeNameField) {
        st_builder.add_service_type_name(type_name_offset);
    }
    if (kind != MalformedConfigKind::kMissingEventsVectorField) {
        st_builder.add_events(events_offset);
    }
    auto const service_type_offset = st_builder.Finish();

    auto const service_types = fbb.CreateVector(&service_type_offset, kSingleElementCount);
    auto const root = mw_someip_config::CreateRoot(fbb, service_types);
    fbb.Finish(root);

    auto const raw_buffer = std::make_shared<std::vector<std::uint8_t>>(
        fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());
    auto const* const root_ptr = mw_someip_config::GetRoot(raw_buffer->data());
    auto const* const st_ptr = root_ptr->service_types()->Get(kFirstElementIndex);
    auto const* const local_ptr = st_ptr->local_service_instances()->Get(kFirstElementIndex);

    return BuiltConfig{
        raw_buffer,
        std::shared_ptr<mw_someip_config::ServiceType const>{raw_buffer, st_ptr},
        std::shared_ptr<mw_someip_config::ServiceInstance const>{raw_buffer, local_ptr},
        nullptr,
    };
}

class LocalServiceInstanceTest : public ServiceInstanceTestFixture {
   protected:
    using ReceiveHandlerWeakPtr = std::weak_ptr<score::mw::com::impl::ScopedEventReceiveHandler>;
    using DiscoveryHandler =
        score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>;

    struct SingleEventProxyPlan {
        score::mw::com::impl::HandleType handle;
        ::testing::NiceMock<score::testing::GenericProxyEvent>* event_mock{nullptr};
    };

    SingleEventProxyPlan PrepareSingleEventProxy(std::string_view const specifier,
                                                 std::string_view const event_name) {
        auto handle = MakeHandle(specifier, {event_name});
        auto* const event_mock = ExpectProxyWithEvent(event_name);
        return SingleEventProxyPlan{std::move(handle), event_mock};
    }

    static void CaptureReceiveHandler(::testing::NiceMock<score::testing::GenericProxyEvent>& event,
                                      ReceiveHandlerWeakPtr& out_handler) {
        EXPECT_CALL(event, SetReceiveHandler(_))
            .WillOnce([&out_handler](ReceiveHandlerWeakPtr handler) noexcept {
                out_handler = std::move(handler);
                return score::Result<void>{};
            });
    }

    static bool TriggerReceiveHandler(ReceiveHandlerWeakPtr const& weak_handler) {
        auto const locked = weak_handler.lock();
        if (locked == nullptr) {
            return false;
        }
        (*locked)();
        return true;
    }

    void ExpectZeroDiscoveryStarts() {
        EXPECT_CALL(service_discovery_mock(),
                    StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
            .Times(0);
        EXPECT_CALL(service_discovery_mock(),
                    StartFindService(_, An<score::mw::com::impl::InstanceIdentifier>()))
            .Times(0);
        EXPECT_CALL(service_discovery_mock(),
                    StartFindService(_, An<score::mw::com::impl::EnrichedInstanceIdentifier>()))
            .Times(0);
    }
};

TEST_F(LocalServiceInstanceTest, CreateSucceedsWithEmptyEventsVector) {
    auto const empty_events_cfg = BuildConfig({});
    auto const handle = MakeHandle(kLocalSpecifier, {});
    std::optional<socom::Server_service_interface_definition> captured_def{};
    std::optional<socom::Service_instance> captured_inst{};

    auto const proxy_mocks = ExpectProxyWithEvents({});
    ASSERT_NE(proxy_mocks.proxy, nullptr);

    auto connector_mock = std::make_unique<::testing::NiceMock<socom::Server_connector_mock>>();
    auto* const connector_ptr = connector_mock.get();
    EXPECT_CALL(*connector_ptr, enable()).WillOnce(Return(connector_ptr));
    EXPECT_CALL(socom_runtime_, make_server_connector(_, _, _))
        .InSequence(server_connector_sequence())
        .WillOnce([&captured_def, &captured_inst, sc = std::move(connector_mock)](
                      socom::Server_service_interface_definition const def,
                      socom::Service_instance const inst,
                      socom::Disabled_server_connector::Callbacks) mutable
                      -> score::Result<socom::Disabled_server_connector::Uptr> {
            captured_def.emplace(def);
            captured_inst.emplace(inst);
            return std::move(sc);
        });

    auto proxy_result = score::mw::com::GenericProxy::Create(handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result =
        LocalServiceInstance::Create(empty_events_cfg.local_instance, empty_events_cfg.service_type,
                                     std::move(proxy_result).value(), socom_runtime_);
    ASSERT_TRUE(instance_result.has_value());
    EXPECT_NE(instance_result.value(), nullptr);
    ASSERT_TRUE(captured_def.has_value());
    EXPECT_EQ(captured_def->get_num_methods(), kZeroMethods);
    EXPECT_EQ(captured_def->get_num_events(), kZeroEvents);
    ASSERT_TRUE(captured_inst.has_value());
    EXPECT_EQ(*captured_inst, socom::Service_instance{kServiceTypeName});
}

TEST_F(LocalServiceInstanceTest, CreateReturnsRequestRejectedWhenServerConnectorCreationFails) {
    auto const plan = PrepareSingleEventProxy(kLocalSpecifier, kEventSerialized);
    ASSERT_NE(plan.event_mock, nullptr);
    EXPECT_CALL(*plan.event_mock, SetReceiveHandler(_)).Times(0);
    EXPECT_CALL(*plan.event_mock, Subscribe(_)).Times(0);
    ExpectServerConnectorCreationFailure(socom::Error::runtime_error_request_rejected);

    auto proxy_result = score::mw::com::GenericProxy::Create(plan.handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result =
        LocalServiceInstance::Create(default_cfg_.local_instance, default_cfg_.service_type,
                                     std::move(proxy_result).value(), socom_runtime_);
    ASSERT_FALSE(instance_result.has_value());
    EXPECT_EQ(instance_result.error(), socom::Error::runtime_error_request_rejected);
}

TEST_F(LocalServiceInstanceTest,
       CreateRegistersServerConnectorDefinitionAndUnsupportedMethodCallbacks) {
    ServiceConfigParams params{};
    params.service_type_name = kCustomServiceName;
    params.service_id = kCustomServiceId;
    params.major_version = kCustomMajorVersion;
    params.minor_version = kCustomMinorVersion;

    auto const cfg = BuildConfig(
        {
            EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
            EventSpec{kEventSerialized2, kEventSerialized2Id, kMaxMessageSize2},
        },
        params);
    ASSERT_NO_FATAL_FAILURE(ReinitSerializer(cfg));

    auto const handle = MakeHandle(kLocalSpecifier, {kEventSerialized, kEventSerialized2});
    std::optional<socom::Server_service_interface_definition> captured_def{};
    std::optional<socom::Service_instance> captured_inst{};
    socom::Disabled_server_connector::Callbacks callbacks{};

    auto const proxy_mocks = ExpectProxyWithEvents({kEventSerialized, kEventSerialized2});
    ASSERT_NE(proxy_mocks.proxy, nullptr);

    auto connector_mock = std::make_unique<::testing::NiceMock<socom::Server_connector_mock>>();
    auto* const connector_ptr = connector_mock.get();
    EXPECT_CALL(*connector_ptr, enable()).WillOnce(Return(connector_ptr));
    EXPECT_CALL(*connector_ptr, allocate_event_payload(_)).Times(0);
    EXPECT_CALL(*connector_ptr, update_event(_, _)).Times(0);
    EXPECT_CALL(socom_runtime_, make_server_connector(_, _, _))
        .InSequence(server_connector_sequence())
        .WillOnce([&captured_def, &captured_inst, &callbacks, sc = std::move(connector_mock)](
                      socom::Server_service_interface_definition const def,
                      socom::Service_instance const inst,
                      socom::Disabled_server_connector::Callbacks cbs) mutable
                      -> score::Result<socom::Disabled_server_connector::Uptr> {
            captured_def.emplace(def);
            captured_inst.emplace(inst);
            callbacks = std::move(cbs);
            return std::move(sc);
        });

    auto proxy_result = score::mw::com::GenericProxy::Create(handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result = LocalServiceInstance::Create(
        cfg.local_instance, cfg.service_type, std::move(proxy_result).value(), socom_runtime_);
    ASSERT_TRUE(instance_result.has_value());

    ASSERT_TRUE(captured_def.has_value());
    EXPECT_EQ(captured_def->get_interface().id.StringView(), kCustomServiceName);
    EXPECT_EQ(captured_def->get_interface().version.major, kCustomMajorVersion);
    EXPECT_EQ(captured_def->get_interface().version.minor, kExpectedMinorVersionU16);
    EXPECT_EQ(captured_def->get_num_methods(), kZeroMethods);
    EXPECT_EQ(captured_def->get_num_events(), kTwoEvents);

    ASSERT_TRUE(captured_inst.has_value());
    EXPECT_EQ(*captured_inst, socom::Service_instance{kCustomServiceName});

    ASSERT_FALSE(callbacks.on_method_call.empty());
    EXPECT_EQ(callbacks.on_method_call(*connector_ptr, kSampleMethodId, socom::empty_payload(),
                                       std::nullopt),
              nullptr);

    ASSERT_FALSE(callbacks.on_method_call_payload_allocate.empty());
    auto const alloc_result =
        callbacks.on_method_call_payload_allocate(*connector_ptr, kSampleMethodId);
    ASSERT_FALSE(alloc_result.has_value());
    EXPECT_EQ(alloc_result.error(), socom::Error::runtime_error_request_rejected);

    ASSERT_FALSE(callbacks.on_event_subscription_change.empty());
    callbacks.on_event_subscription_change(*connector_ptr, kPositionalEventId0,
                                           socom::Event_state::subscribed);

    ASSERT_FALSE(callbacks.on_event_update_request.empty());
    callbacks.on_event_update_request(*connector_ptr, kPositionalEventId0);
}

TEST_F(LocalServiceInstanceTest, CreateAsyncPropagatesStartFindServiceFailure) {
    auto const expected_specifier =
        score::mw::com::impl::InstanceSpecifier::Create(std::string{kLocalSpecifier}).value();
    EXPECT_CALL(service_discovery_mock(), StartFindService(_, expected_specifier))
        .WillOnce(
            Return(score::MakeUnexpected(score::mw::com::ComErrc::kFindServiceHandlerFailure)));

    std::vector<std::unique_ptr<LocalServiceInstance>> instances{};
    auto const result = LocalServiceInstance::CreateAsyncLocalServices(
        default_cfg_.local_instance, default_cfg_.service_type, socom_runtime_, instances);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), score::mw::com::ComErrc::kFindServiceHandlerFailure);
    EXPECT_EQ(instances.size(), kExpectedZeroInstances);
}

TEST_F(LocalServiceInstanceTest, CreateAsyncRetainsDiscoveryOnCreationFailuresThenRecovers) {
    auto const expected_specifier =
        score::mw::com::impl::InstanceSpecifier::Create(std::string{kLocalSpecifier}).value();
    auto const find_handle = score::mw::com::impl::make_FindServiceHandle(kFindHandleUid);
    auto const handle = MakeHandle(kLocalSpecifier, {kEventSerialized});
    DiscoveryHandler captured_handler{};
    std::size_t stop_find_calls{0U};

    EXPECT_CALL(service_discovery_mock(), StartFindService(_, expected_specifier))
        .WillOnce([&captured_handler, find_handle](DiscoveryHandler handler,
                                                   score::mw::com::impl::InstanceSpecifier) {
            captured_handler = std::move(handler);
            return score::Result<score::mw::com::impl::FindServiceHandle>{find_handle};
        });

    EXPECT_CALL(service_discovery_mock(), StopFindService(find_handle))
        .WillOnce([&stop_find_calls](score::mw::com::impl::FindServiceHandle) {
            ++stop_find_calls;
            return score::Result<void>{};
        });

    ExpectProxyCreationFailure();
    (void)ExpectProxyWithEvent(kEventSerialized);
    ExpectServerConnectorCreationFailure(socom::Error::runtime_error_request_rejected);
    (void)ExpectProxyWithEvent(kEventSerialized);
    (void)ExpectServerConnectorCreated();

    std::vector<std::unique_ptr<LocalServiceInstance>> instances{};
    auto const start_result = LocalServiceInstance::CreateAsyncLocalServices(
        default_cfg_.local_instance, default_cfg_.service_type, socom_runtime_, instances);
    ASSERT_TRUE(start_result.has_value());
    EXPECT_EQ(start_result.value(), find_handle);

    // Attempt 1: GenericProxy::Create fails -> discovery remains active.
    captured_handler({handle}, find_handle);
    EXPECT_EQ(instances.size(), kExpectedZeroInstances);
    EXPECT_EQ(stop_find_calls, kZeroBytes);

    // Attempt 2: LocalServiceInstance::Create fails -> discovery remains active.
    captured_handler({handle}, find_handle);
    EXPECT_EQ(instances.size(), kExpectedZeroInstances);
    EXPECT_EQ(stop_find_calls, kZeroBytes);

    // Attempt 3: Valid handle succeeds and stops discovery.
    captured_handler({handle}, find_handle);
    ASSERT_EQ(instances.size(), kExpectedSingleInstance);
    EXPECT_NE(instances[0], nullptr);
    EXPECT_EQ(stop_find_calls, kExpectedSingleCall);
}

TEST_F(LocalServiceInstanceTest, DropsNullSampleAndIgnoresGetNewSamplesFailure) {
    CanaryBackingBuffer fallback_buf{kStandardPayloadBufferCapacity};
    ReceiveHandlerWeakPtr handler{};

    auto const plan = PrepareSingleEventProxy(kLocalSpecifier, kEventSerialized);
    ASSERT_NE(plan.event_mock, nullptr);
    CaptureReceiveHandler(*plan.event_mock, handler);
    EXPECT_CALL(*plan.event_mock, GetNewSamples(_, _))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::ComErrc::kBindingFailure)))
        .WillOnce([](auto&& callback, score::mw::com::impl::TrackerGuardFactory&) {
            InvokeSampleCallback(callback, score::mw::com::impl::SamplePtr<void>{});
            return score::Result<std::size_t>{kOneByte};
        });

    auto* const connector_ptr = ExpectServerConnectorCreated();
    ASSERT_NE(connector_ptr, nullptr);
    ON_CALL(*connector_ptr, allocate_event_payload(_))
        .WillByDefault([&fallback_buf](socom::Event_id) noexcept {
            return fallback_buf.MakePayload(kStandardPayloadBufferCapacity);
        });
    EXPECT_CALL(*connector_ptr, update_event(_, _)).Times(0);

    auto proxy_result = score::mw::com::GenericProxy::Create(plan.handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result =
        LocalServiceInstance::Create(default_cfg_.local_instance, default_cfg_.service_type,
                                     std::move(proxy_result).value(), socom_runtime_);
    ASSERT_TRUE(instance_result.has_value());

    ASSERT_TRUE(TriggerReceiveHandler(handler));
    ASSERT_TRUE(TriggerReceiveHandler(handler));
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest,
       DISABLED_CreateSkipsMissingIpcAndUnserializedEventsPreservingPositionalEventIds) {
    auto const cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
        EventSpec{kEventMissingInIpc, kEventMissingInIpcId, kMaxMessageSize},
        EventSpec{kEventNoSerializer, kEventNoSerializerId, std::nullopt},
        EventSpec{kEventSerialized2, kEventSerialized2Id, kMaxMessageSize2},
    });
    ASSERT_NO_FATAL_FAILURE(ReinitSerializer(cfg));

    auto const handle =
        MakeHandle(kLocalSpecifier, {kEventSerialized, kEventNoSerializer, kEventSerialized2});
    auto sample0 = MakePreSerializedSample<kMaxMessageSize>({0xA1U, 0xB2U});
    auto sample3 = MakePreSerializedSample<kMaxMessageSize2>({0xC3U, 0xD4U});
    CanaryBackingBuffer buffer0{kStandardPayloadBufferCapacity};
    CanaryBackingBuffer buffer3{kStandardPayloadBufferCapacity};
    std::vector<CapturedEventFrame> forwarded{};
    std::optional<socom::Server_service_interface_definition> captured_def{};
    ReceiveHandlerWeakPtr ev0_handler{};
    ReceiveHandlerWeakPtr ev3_handler{};

    auto const proxy_mocks =
        ExpectProxyWithEvents({kEventSerialized, kEventNoSerializer, kEventSerialized2});
    ASSERT_EQ(proxy_mocks.events.size(), kThreeProxyEvents);
    auto* const ev0_mock = proxy_mocks.events[0];
    auto* const ev_no_ser_mock = proxy_mocks.events[1];
    auto* const ev3_mock = proxy_mocks.events[2];

    CaptureReceiveHandler(*ev0_mock, ev0_handler);
    EXPECT_CALL(*ev0_mock, Subscribe(score::someip::kMaxSampleCount))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*ev0_mock, GetNewSamples(_, _))
        .WillOnce([&sample0](auto&& callback, score::mw::com::impl::TrackerGuardFactory& tracker) {
            InvokeSampleCallback(callback, score::testing::MakeMockSample(&sample0, tracker));
            return score::Result<std::size_t>{kOneByte};
        });

    EXPECT_CALL(*ev_no_ser_mock, SetReceiveHandler(_)).Times(0);
    EXPECT_CALL(*ev_no_ser_mock, Subscribe(_)).Times(0);

    CaptureReceiveHandler(*ev3_mock, ev3_handler);
    EXPECT_CALL(*ev3_mock, Subscribe(score::someip::kMaxSampleCount))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*ev3_mock, GetNewSamples(_, _))
        .WillOnce([&sample3](auto&& callback, score::mw::com::impl::TrackerGuardFactory& tracker) {
            InvokeSampleCallback(callback, score::testing::MakeMockSample(&sample3, tracker));
            return score::Result<std::size_t>{kOneByte};
        });

    auto connector_mock = std::make_unique<::testing::NiceMock<socom::Server_connector_mock>>();
    auto* const connector_ptr = connector_mock.get();
    ON_CALL(*connector_ptr, enable()).WillByDefault(Return(connector_ptr));
    EXPECT_CALL(*connector_ptr, allocate_event_payload(kPositionalEventId0))
        .WillOnce([&buffer0](socom::Event_id) noexcept {
            return buffer0.MakePayload(kStandardPayloadBufferCapacity);
        });
    EXPECT_CALL(*connector_ptr, update_event(kPositionalEventId0, _))
        .WillOnce([&forwarded](socom::Event_id const id, socom::Payload const payload) noexcept {
            forwarded.push_back(CapturedEventFrame{id, CopyPayloadBytes(payload)});
            return score::Result<void>{};
        });
    EXPECT_CALL(*connector_ptr, allocate_event_payload(kPositionalEventId3))
        .WillOnce([&buffer3](socom::Event_id) noexcept {
            return buffer3.MakePayload(kStandardPayloadBufferCapacity);
        });
    EXPECT_CALL(*connector_ptr, update_event(kPositionalEventId3, _))
        .WillOnce([&forwarded](socom::Event_id const id, socom::Payload const payload) noexcept {
            forwarded.push_back(CapturedEventFrame{id, CopyPayloadBytes(payload)});
            return score::Result<void>{};
        });
    EXPECT_CALL(socom_runtime_, make_server_connector(_, _, _))
        .InSequence(server_connector_sequence())
        .WillOnce([&captured_def, sc = std::move(connector_mock)](
                      socom::Server_service_interface_definition const def, socom::Service_instance,
                      socom::Disabled_server_connector::Callbacks) mutable
                      -> score::Result<socom::Disabled_server_connector::Uptr> {
            captured_def.emplace(def);
            return std::move(sc);
        });

    auto proxy_result = score::mw::com::GenericProxy::Create(handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result = LocalServiceInstance::Create(
        cfg.local_instance, cfg.service_type, std::move(proxy_result).value(), socom_runtime_);
    ASSERT_TRUE(instance_result.has_value());
    ASSERT_TRUE(captured_def.has_value());
    EXPECT_EQ(captured_def->get_num_events(), kFourEvents);

    ASSERT_TRUE(TriggerReceiveHandler(ev0_handler));
    ASSERT_TRUE(TriggerReceiveHandler(ev3_handler));

    ASSERT_EQ(forwarded.size(), kExpectedTwoFrames);
    EXPECT_EQ(forwarded[0].event_id, kPositionalEventId0);
    ASSERT_EQ(forwarded[0].bytes.size(), kFirstEventWireFrame.size());
    EXPECT_THAT(forwarded[0].bytes, ElementsAreArray(kFirstEventWireFrame));
    EXPECT_EQ(forwarded[1].event_id, kPositionalEventId3);
    ASSERT_EQ(forwarded[1].bytes.size(), kFourthEventWireFrame.size());
    EXPECT_THAT(forwarded[1].bytes, ElementsAreArray(kFourthEventWireFrame));
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest, DISABLED_CreateSkipsSubscribeWhenSetReceiveHandlerFails) {
    auto const cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
        EventSpec{kEventSerialized2, kEventSerialized2Id, kMaxMessageSize2},
    });
    auto const handle = MakeHandle(kLocalSpecifier, {kEventSerialized, kEventSerialized2});
    ReceiveHandlerWeakPtr valid_handler{};

    auto const proxy_mocks = ExpectProxyWithEvents({kEventSerialized, kEventSerialized2});
    ASSERT_EQ(proxy_mocks.events.size(), kTwoEvents);
    auto* const failing_event = proxy_mocks.events[0];
    auto* const valid_event = proxy_mocks.events[1];

    EXPECT_CALL(*failing_event, SetReceiveHandler(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::ComErrc::kBindingFailure)));
    EXPECT_CALL(*failing_event, Subscribe(_)).Times(0);
    EXPECT_CALL(*failing_event, UnsetReceiveHandler()).Times(0);

    CaptureReceiveHandler(*valid_event, valid_handler);
    EXPECT_CALL(*valid_event, Subscribe(score::someip::kMaxSampleCount))
        .WillOnce(Return(score::Result<void>{}));

    auto* const connector_ptr = ExpectServerConnectorCreated();
    ASSERT_NE(connector_ptr, nullptr);

    auto proxy_result = score::mw::com::GenericProxy::Create(handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result = LocalServiceInstance::Create(
        cfg.local_instance, cfg.service_type, std::move(proxy_result).value(), socom_runtime_);
    ASSERT_TRUE(instance_result.has_value());
    EXPECT_FALSE(valid_handler.expired());
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest, DISABLED_CreateUnsetsReceiveHandlerImmediatelyWhenSubscribeFails) {
    auto const cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
        EventSpec{kEventSerialized2, kEventSerialized2Id, kMaxMessageSize2},
    });
    auto const handle = MakeHandle(kLocalSpecifier, {kEventSerialized, kEventSerialized2});
    std::size_t unset_calls_on_failed_event{0U};
    ReceiveHandlerWeakPtr valid_handler{};

    auto const proxy_mocks = ExpectProxyWithEvents({kEventSerialized, kEventSerialized2});
    ASSERT_EQ(proxy_mocks.events.size(), kTwoEvents);
    auto* const subscribe_fail_event = proxy_mocks.events[0];
    auto* const valid_event = proxy_mocks.events[1];

    EXPECT_CALL(*subscribe_fail_event, SetReceiveHandler(_))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*subscribe_fail_event, Subscribe(score::someip::kMaxSampleCount))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::ComErrc::kBindingFailure)));
    EXPECT_CALL(*subscribe_fail_event, UnsetReceiveHandler())
        .WillOnce([&unset_calls_on_failed_event]() noexcept {
            ++unset_calls_on_failed_event;
            return score::Result<void>{};
        });
    EXPECT_CALL(*subscribe_fail_event, Unsubscribe()).Times(0);

    CaptureReceiveHandler(*valid_event, valid_handler);
    EXPECT_CALL(*valid_event, Subscribe(score::someip::kMaxSampleCount))
        .WillOnce(Return(score::Result<void>{}));

    (void)ExpectServerConnectorCreated();

    auto proxy_result = score::mw::com::GenericProxy::Create(handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result = LocalServiceInstance::Create(
        cfg.local_instance, cfg.service_type, std::move(proxy_result).value(), socom_runtime_);
    ASSERT_TRUE(instance_result.has_value());
    EXPECT_EQ(unset_calls_on_failed_event, kExpectedSingleCall);
    EXPECT_FALSE(valid_handler.expired());
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest, DISABLED_ForwardsCanonicalSomeipWireVectorWithSignBitBoundaries) {
    ServiceConfigParams params{};
    params.service_id = kCanonicalServiceId;
    params.major_version = kCanonicalMajorVersion;
    params.minor_version = kCanonicalMinorVersion;

    auto const cfg =
        BuildConfig({EventSpec{kEventSerialized, kCanonicalEventId, kMaxMessageSize}}, params);
    ASSERT_NO_FATAL_FAILURE(ReinitSerializer(cfg));

    auto sample = MakePreSerializedSample<kMaxMessageSize>({0x00U, 0x7FU, 0x80U, 0xFFU});
    CanaryBackingBuffer backing{kStandardPayloadBufferCapacity, kPrefillSentinelByte};
    std::vector<CapturedEventFrame> captured{};
    ReceiveHandlerWeakPtr handler{};

    auto const plan = PrepareSingleEventProxy(kLocalSpecifier, kEventSerialized);
    ASSERT_NE(plan.event_mock, nullptr);
    CaptureReceiveHandler(*plan.event_mock, handler);
    EXPECT_CALL(*plan.event_mock, Subscribe(score::someip::kMaxSampleCount))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*plan.event_mock, GetNewSamples(_, _))
        .WillOnce([&sample](auto&& callback, score::mw::com::impl::TrackerGuardFactory& tracker) {
            InvokeSampleCallback(callback, score::testing::MakeMockSample(&sample, tracker));
            return score::Result<std::size_t>{kOneByte};
        });

    auto* const connector_ptr = ExpectServerConnectorCreated();
    ASSERT_NE(connector_ptr, nullptr);
    EXPECT_CALL(*connector_ptr, allocate_event_payload(kPositionalEventId0))
        .WillOnce([&backing](socom::Event_id) noexcept {
            return backing.MakePayload(kStandardPayloadBufferCapacity);
        });
    EXPECT_CALL(*connector_ptr, update_event(kPositionalEventId0, _))
        .WillOnce([&captured](socom::Event_id const id, socom::Payload const payload) noexcept {
            captured.push_back(CapturedEventFrame{id, CopyPayloadBytes(payload)});
            return score::Result<void>{};
        });

    auto proxy_result = score::mw::com::GenericProxy::Create(plan.handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result = LocalServiceInstance::Create(
        cfg.local_instance, cfg.service_type, std::move(proxy_result).value(), socom_runtime_);
    ASSERT_TRUE(instance_result.has_value());

    ASSERT_TRUE(TriggerReceiveHandler(handler));
    ASSERT_EQ(captured.size(), kExpectedSingleCall);
    EXPECT_EQ(captured[0].event_id, kPositionalEventId0);
    ASSERT_EQ(captured[0].bytes.size(), kCanonicalTwentyByteWireVector.size());
    EXPECT_THAT(captured[0].bytes, ElementsAreArray(kCanonicalTwentyByteWireVector));
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest, DISABLED_ForwardsZeroByteAndMultiByteLengthPayloads) {
    ServiceConfigParams params{};
    params.service_id = kCanonicalServiceId;
    params.major_version = kCanonicalMajorVersion;
    params.minor_version = kCanonicalMinorVersion;

    auto const cfg = BuildConfig(
        {EventSpec{kEventSerialized, kCanonicalEventId, kMultiByteLengthMaxMessageSize}}, params);
    ASSERT_NO_FATAL_FAILURE(ReinitSerializer(cfg));

    // Boundary 1: 0-byte serialized payload inside exact 16-byte Writable_payload.
    auto empty_sample = MakePreSerializedSample<kMultiByteLengthMaxMessageSize>({});
    CanaryBackingBuffer exact_header_buffer{kHeaderOnlySixteenBytes, kPrefillSentinelByte};

    // Boundary 2: 250-byte serialized payload producing big-endian Length = 258 (0x00000102).
    serializer::PreSerializedData<kMultiByteLengthMaxMessageSize> large_sample{};
    large_sample.size = kMultiBytePayloadSize;
    std::fill_n(large_sample.data, kMultiBytePayloadSize,
                static_cast<std::byte>(kMultiBytePayloadPattern));
    CanaryBackingBuffer large_buffer{kMultiByteFrameCapacity, kPrefillSentinelByte};

    std::vector<CapturedEventFrame> captured{};
    ReceiveHandlerWeakPtr handler{};

    auto const plan = PrepareSingleEventProxy(kLocalSpecifier, kEventSerialized);
    ASSERT_NE(plan.event_mock, nullptr);
    CaptureReceiveHandler(*plan.event_mock, handler);
    EXPECT_CALL(*plan.event_mock, GetNewSamples(_, _))
        .WillOnce([&empty_sample](auto&& callback,
                                  score::mw::com::impl::TrackerGuardFactory& tracker) {
            InvokeSampleCallback(callback, score::testing::MakeMockSample(&empty_sample, tracker));
            return score::Result<std::size_t>{kOneByte};
        })
        .WillOnce([&large_sample](auto&& callback,
                                  score::mw::com::impl::TrackerGuardFactory& tracker) {
            InvokeSampleCallback(callback, score::testing::MakeMockSample(&large_sample, tracker));
            return score::Result<std::size_t>{kOneByte};
        });

    auto* const connector_ptr = ExpectServerConnectorCreated();
    ASSERT_NE(connector_ptr, nullptr);
    EXPECT_CALL(*connector_ptr, allocate_event_payload(kPositionalEventId0))
        .WillOnce([&exact_header_buffer](socom::Event_id) noexcept {
            return exact_header_buffer.MakePayload(kHeaderOnlySixteenBytes);
        })
        .WillOnce([&large_buffer](socom::Event_id) noexcept {
            return large_buffer.MakePayload(kMultiByteFrameCapacity);
        });
    EXPECT_CALL(*connector_ptr, update_event(kPositionalEventId0, _))
        .Times(static_cast<int>(kExpectedTwoFrames))
        .WillRepeatedly(
            [&captured](socom::Event_id const id, socom::Payload const payload) noexcept {
                captured.push_back(CapturedEventFrame{id, CopyPayloadBytes(payload)});
                return score::Result<void>{};
            });

    auto proxy_result = score::mw::com::GenericProxy::Create(plan.handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result = LocalServiceInstance::Create(
        cfg.local_instance, cfg.service_type, std::move(proxy_result).value(), socom_runtime_);
    ASSERT_TRUE(instance_result.has_value());

    ASSERT_TRUE(TriggerReceiveHandler(handler));
    ASSERT_TRUE(TriggerReceiveHandler(handler));

    ASSERT_EQ(captured.size(), kExpectedTwoFrames);
    ASSERT_EQ(captured[0].bytes.size(), kCanonicalZeroPayloadWireVector.size());
    EXPECT_THAT(captured[0].bytes, ElementsAreArray(kCanonicalZeroPayloadWireVector));

    std::vector<std::uint8_t> expected_multi_byte_frame(kMultiByteLengthHeaderPrefix.cbegin(),
                                                        kMultiByteLengthHeaderPrefix.cend());
    expected_multi_byte_frame.insert(expected_multi_byte_frame.cend(), kMultiBytePayloadSize,
                                     kMultiBytePayloadPattern);
    ASSERT_EQ(captured[1].bytes.size(), kMultiByteExpectedFrameSize);
    EXPECT_EQ(captured[1].bytes, expected_multi_byte_frame);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest, DISABLED_RecoversAcrossValidUndersizedAndValidPayloadAllocations) {
    auto valid_sample_one = MakePreSerializedSample<kMaxMessageSize>({0x11U, 0x22U, 0x33U, 0x44U});
    auto failing_sample = MakePreSerializedSample<kMaxMessageSize>({0x55U, 0x66U, 0x77U, 0x88U});
    auto valid_sample_two = MakePreSerializedSample<kMaxMessageSize>({0x99U, 0xAAU, 0xBBU, 0xCCU});

    // Valid (20B = 16 + 4) -> Failing (19B = 16 + 4 - 1) -> Valid (20B = 16 + 4) seam.
    CanaryBackingBuffer valid_buf_one{kExactTwentyBytes};
    CanaryBackingBuffer undersized_payload_buf{kOffByOneNineteenBytes};
    CanaryBackingBuffer valid_buf_two{kExactTwentyBytes};
    std::vector<CapturedEventFrame> captured{};
    ReceiveHandlerWeakPtr handler{};

    auto const plan = PrepareSingleEventProxy(kLocalSpecifier, kEventSerialized);
    ASSERT_NE(plan.event_mock, nullptr);
    CaptureReceiveHandler(*plan.event_mock, handler);
    EXPECT_CALL(*plan.event_mock, GetNewSamples(_, _))
        .WillOnce([&valid_sample_one](auto&& callback,
                                      score::mw::com::impl::TrackerGuardFactory& tracker) {
            InvokeSampleCallback(callback,
                                 score::testing::MakeMockSample(&valid_sample_one, tracker));
            return score::Result<std::size_t>{kOneByte};
        })
        .WillOnce(
            [&failing_sample](auto&& callback, score::mw::com::impl::TrackerGuardFactory& tracker) {
                InvokeSampleCallback(callback,
                                     score::testing::MakeMockSample(&failing_sample, tracker));
                return score::Result<std::size_t>{kOneByte};
            })
        .WillOnce([&valid_sample_two](auto&& callback,
                                      score::mw::com::impl::TrackerGuardFactory& tracker) {
            InvokeSampleCallback(callback,
                                 score::testing::MakeMockSample(&valid_sample_two, tracker));
            return score::Result<std::size_t>{kOneByte};
        });

    auto* const connector_ptr = ExpectServerConnectorCreated();
    ASSERT_NE(connector_ptr, nullptr);
    EXPECT_CALL(*connector_ptr, allocate_event_payload(kPositionalEventId0))
        .WillOnce([&valid_buf_one](socom::Event_id) noexcept {
            return valid_buf_one.MakePayload(kExactTwentyBytes);
        })
        .WillOnce([&undersized_payload_buf](socom::Event_id) noexcept {
            return undersized_payload_buf.MakePayload(kOffByOneNineteenBytes);
        })
        .WillOnce([&valid_buf_two](socom::Event_id) noexcept {
            return valid_buf_two.MakePayload(kExactTwentyBytes);
        });
    EXPECT_CALL(*connector_ptr, update_event(kPositionalEventId0, _))
        .Times(static_cast<int>(kExpectedTwoFrames))
        .WillRepeatedly(
            [&captured](socom::Event_id const id, socom::Payload const payload) noexcept {
                captured.push_back(CapturedEventFrame{id, CopyPayloadBytes(payload)});
                return score::Result<void>{};
            });

    auto proxy_result = score::mw::com::GenericProxy::Create(plan.handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result =
        LocalServiceInstance::Create(default_cfg_.local_instance, default_cfg_.service_type,
                                     std::move(proxy_result).value(), socom_runtime_);
    ASSERT_TRUE(instance_result.has_value());

    ASSERT_TRUE(TriggerReceiveHandler(handler));
    ASSERT_TRUE(TriggerReceiveHandler(handler));
    ASSERT_TRUE(TriggerReceiveHandler(handler));

    ASSERT_EQ(captured.size(), kExpectedTwoFrames);
    ASSERT_EQ(captured[0].bytes.size(), kSeamValidFrameOne.size());
    EXPECT_THAT(captured[0].bytes, ElementsAreArray(kSeamValidFrameOne));
    ASSERT_EQ(captured[1].bytes.size(), kSeamValidFrameTwo.size());
    EXPECT_THAT(captured[1].bytes, ElementsAreArray(kSeamValidFrameTwo));
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest,
       DISABLED_ForwardsMultiSampleBatchWhileSkippingFailedPayloadAllocation) {
    auto batch_sample_one = MakePreSerializedSample<kMaxMessageSize>({0xA1U, 0xB2U});
    auto dropped_sample = MakePreSerializedSample<kMaxMessageSize>({0xDEU, 0xADU});
    auto batch_sample_two = MakePreSerializedSample<kMaxMessageSize>({0x11U, 0x22U, 0x33U, 0x44U});

    CanaryBackingBuffer batch_buf_one{kStandardPayloadBufferCapacity};
    CanaryBackingBuffer batch_buf_two{kStandardPayloadBufferCapacity};
    std::vector<CapturedEventFrame> captured{};
    ReceiveHandlerWeakPtr handler{};

    auto const plan = PrepareSingleEventProxy(kLocalSpecifier, kEventSerialized);
    ASSERT_NE(plan.event_mock, nullptr);
    CaptureReceiveHandler(*plan.event_mock, handler);
    EXPECT_CALL(*plan.event_mock, GetNewSamples(_, _))
        .WillOnce([&batch_sample_one, &dropped_sample, &batch_sample_two](
                      auto&& callback, score::mw::com::impl::TrackerGuardFactory& tracker) {
            InvokeSampleCallback(callback,
                                 score::testing::MakeMockSample(&batch_sample_one, tracker));
            InvokeSampleCallback(callback,
                                 score::testing::MakeMockSample(&dropped_sample, tracker));
            InvokeSampleCallback(callback,
                                 score::testing::MakeMockSample(&batch_sample_two, tracker));
            return score::Result<std::size_t>{kThreeSampleBatchCount};
        });

    auto* const connector_ptr = ExpectServerConnectorCreated();
    ASSERT_NE(connector_ptr, nullptr);
    EXPECT_CALL(*connector_ptr, allocate_event_payload(kPositionalEventId0))
        .WillOnce([&batch_buf_one](socom::Event_id) noexcept {
            return batch_buf_one.MakePayload(kStandardPayloadBufferCapacity);
        })
        .WillOnce(Return(score::MakeUnexpected(
            socom::Server_connector_error::runtime_error_no_client_subscribed_for_event)))
        .WillOnce([&batch_buf_two](socom::Event_id) noexcept {
            return batch_buf_two.MakePayload(kStandardPayloadBufferCapacity);
        });
    EXPECT_CALL(*connector_ptr, update_event(kPositionalEventId0, _))
        .Times(static_cast<int>(kExpectedTwoFrames))
        .WillRepeatedly(
            [&captured](socom::Event_id const id, socom::Payload const payload) noexcept {
                captured.push_back(CapturedEventFrame{id, CopyPayloadBytes(payload)});
                return score::Result<void>{};
            });

    auto proxy_result = score::mw::com::GenericProxy::Create(plan.handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result =
        LocalServiceInstance::Create(default_cfg_.local_instance, default_cfg_.service_type,
                                     std::move(proxy_result).value(), socom_runtime_);
    ASSERT_TRUE(instance_result.has_value());

    ASSERT_TRUE(TriggerReceiveHandler(handler));
    ASSERT_EQ(captured.size(), kExpectedTwoFrames);
    ASSERT_EQ(captured[0].bytes.size(), kFirstEventWireFrame.size());
    EXPECT_THAT(captured[0].bytes, ElementsAreArray(kFirstEventWireFrame));
    ASSERT_EQ(captured[1].bytes.size(), kSeamValidFrameOne.size());
    EXPECT_THAT(captured[1].bytes, ElementsAreArray(kSeamValidFrameOne));
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest,
       DISABLED_DestructorUnsetsReceiveHandlersAndUnsubscribesActiveEvents) {
    auto const cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
        EventSpec{kEventNoSerializer, kEventNoSerializerId, std::nullopt},
    });
    auto const handle = MakeHandle(kLocalSpecifier, {kEventSerialized, kEventNoSerializer});
    ReceiveHandlerWeakPtr active_handler{};
    std::size_t unset_count{0U};
    std::size_t unsubscribe_count{0U};

    auto const proxy_mocks = ExpectProxyWithEvents({kEventSerialized, kEventNoSerializer});
    ASSERT_EQ(proxy_mocks.events.size(), kTwoEvents);
    auto* const active_event = proxy_mocks.events[0];
    auto* const skipped_event = proxy_mocks.events[1];

    CaptureReceiveHandler(*active_event, active_handler);
    EXPECT_CALL(*active_event, Subscribe(score::someip::kMaxSampleCount))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*active_event, UnsetReceiveHandler()).WillOnce([&unset_count]() noexcept {
        ++unset_count;
        return score::Result<void>{};
    });
    EXPECT_CALL(*active_event, Unsubscribe()).WillOnce([&unsubscribe_count]() noexcept {
        ++unsubscribe_count;
    });

    EXPECT_CALL(*skipped_event, SetReceiveHandler(_)).Times(0);
    EXPECT_CALL(*skipped_event, Subscribe(_)).Times(0);
    EXPECT_CALL(*skipped_event, UnsetReceiveHandler()).Times(0);
    EXPECT_CALL(*skipped_event, Unsubscribe()).Times(0);

    (void)ExpectServerConnectorCreated();

    auto proxy_result = score::mw::com::GenericProxy::Create(handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result = LocalServiceInstance::Create(
        cfg.local_instance, cfg.service_type, std::move(proxy_result).value(), socom_runtime_);
    ASSERT_TRUE(instance_result.has_value());
    EXPECT_EQ(unset_count, kZeroBytes);
    EXPECT_EQ(unsubscribe_count, kZeroBytes);
    EXPECT_FALSE(active_handler.expired());

    instance_result.value().reset();

    EXPECT_EQ(unset_count, kExpectedSingleCall);
    EXPECT_EQ(unsubscribe_count, kExpectedSingleCall);
    EXPECT_TRUE(active_handler.expired());
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest,
       DISABLED_CreateAsyncCreatesFromFrontHandleAndIgnoresDuplicateCallbacks) {
    auto const expected_specifier =
        score::mw::com::impl::InstanceSpecifier::Create(std::string{kLocalSpecifier}).value();
    auto const find_handle = score::mw::com::impl::make_FindServiceHandle(kFindHandleUid);
    DiscoveryHandler captured_handler{};

    EXPECT_CALL(service_discovery_mock(), StartFindService(_, expected_specifier))
        .WillOnce([&captured_handler, find_handle](DiscoveryHandler handler,
                                                   score::mw::com::impl::InstanceSpecifier) {
            captured_handler = std::move(handler);
            return score::Result<score::mw::com::impl::FindServiceHandle>{find_handle};
        });

    std::size_t stop_find_calls{0U};
    EXPECT_CALL(service_discovery_mock(), StopFindService(find_handle))
        .WillOnce([&stop_find_calls](score::mw::com::impl::FindServiceHandle) {
            ++stop_find_calls;
            return score::Result<void>{};
        });

    auto const front_handle = MakeHandle(kLocalSpecifier, {kEventSerialized}, kPrimaryInstanceId);
    auto const second_handle =
        MakeHandle(kLocalSpecifier, {kEventSerialized}, kSecondaryInstanceId);

    auto proxy_mock = std::make_unique<::testing::NiceMock<score::testing::Proxy>>();
    auto* const proxy_ptr = proxy_mock.get();
    ON_CALL(*proxy_ptr, IsEventProvided(_)).WillByDefault(Return(false));
    auto event_mock = std::make_unique<::testing::NiceMock<score::testing::GenericProxyEvent>>();
    EXPECT_CALL(*proxy_ptr, IsEventProvided(kEventSerialized)).WillOnce(Return(true));
    EXPECT_CALL(generic_proxy_event_factory_mock(),
                Create(_, ::testing::Ref(*proxy_ptr), kEventSerialized, _))
        .WillOnce(Return(::testing::ByMove(std::move(event_mock))));
    EXPECT_CALL(proxy_binding_factory_mock(), Create(front_handle))
        .InSequence(proxy_factory_sequence())
        .WillOnce(Return(::testing::ByMove(std::move(proxy_mock))))
        .RetiresOnSaturation();
    (void)ExpectServerConnectorCreated();

    std::vector<std::unique_ptr<LocalServiceInstance>> instances{};
    auto const start_result = LocalServiceInstance::CreateAsyncLocalServices(
        default_cfg_.local_instance, default_cfg_.service_type, socom_runtime_, instances);
    ASSERT_TRUE(start_result.has_value());

    // Multi-handle notification creates 1 instance from front() and stops discovery.
    captured_handler({front_handle, second_handle}, find_handle);
    ASSERT_EQ(instances.size(), kExpectedSingleInstance);
    EXPECT_NE(instances[0], nullptr);
    EXPECT_EQ(stop_find_calls, kExpectedSingleCall);

    // Duplicate callback after completion is ignored without creating a second instance.
    captured_handler({front_handle}, find_handle);
    EXPECT_EQ(instances.size(), kExpectedSingleInstance);
    EXPECT_EQ(stop_find_calls, kExpectedSingleCall);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest, DISABLED_CreateAsyncRejectsInvalidConfigurationsAndSpecifiers) {
    ExpectZeroDiscoveryStarts();

    constexpr std::array<std::string_view, kMalformedSpecifierCount> kMalformedSpecifiers{{
        "",
        "gatewayd//local_echo",
        "1gatewayd/local_echo",
        "gatewayd/local_echo/",
    }};

    for (std::string_view const bad_specifier : kMalformedSpecifiers) {
        SCOPED_TRACE(std::string{"specifier_precheck="} + std::string{bad_specifier});
        ASSERT_FALSE(score::mw::com::impl::InstanceSpecifier::Create(std::string{bad_specifier})
                         .has_value());
    }

    for (std::string_view const bad_specifier : kMalformedSpecifiers) {
        SCOPED_TRACE(std::string{"specifier="} + std::string{bad_specifier});
        ServiceConfigParams params{};
        params.local_specifier = bad_specifier;
        auto const cfg =
            BuildConfig({EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize}}, params);

        std::vector<std::unique_ptr<LocalServiceInstance>> instances{};
        auto const result = LocalServiceInstance::CreateAsyncLocalServices(
            cfg.local_instance, cfg.service_type, socom_runtime_, instances);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), score::mw::com::ComErrc::kInvalidConfiguration);
        EXPECT_EQ(instances.size(), kExpectedZeroInstances);
    }

    struct NullFieldCase {
        const char* label;
        MalformedConfigKind kind;
    };

    constexpr std::array<NullFieldCase, kInvalidAsyncNullCaseCount> kNullCases{{
        {"null_service_instance_config", MalformedConfigKind::kNullServiceInstance},
        {"missing_instance_specifier", MalformedConfigKind::kMissingInstanceSpecifierField},
        {"null_service_type_config", MalformedConfigKind::kNullServiceType},
        {"missing_service_type_name", MalformedConfigKind::kMissingServiceTypeNameField},
        {"null_events_vector", MalformedConfigKind::kMissingEventsVectorField},
        {"missing_event_name", MalformedConfigKind::kMissingEventNameField},
    }};

    for (auto const& test_case : kNullCases) {
        SCOPED_TRACE(test_case.label);
        auto const cfg = BuildRawConfigVariant(test_case.kind);
        std::vector<std::unique_ptr<LocalServiceInstance>> instances{};
        auto const result = LocalServiceInstance::CreateAsyncLocalServices(
            cfg.local_instance, cfg.service_type, socom_runtime_, instances);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), score::mw::com::ComErrc::kInvalidConfiguration);
        EXPECT_EQ(instances.size(), kExpectedZeroInstances);
    }
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest, DISABLED_CreateAsyncSynchronizesConcurrentDiscoveryCallbacks) {
    std::vector<BuiltConfig> configs{};
    configs.reserve(kConcurrentWorkerCount);
    std::vector<DiscoveryHandler> handlers(kConcurrentWorkerCount);
    std::vector<score::mw::com::impl::FindServiceHandle> find_handles{};
    find_handles.reserve(kConcurrentWorkerCount);
    std::vector<score::mw::com::impl::HandleType> service_handles{};
    service_handles.reserve(kConcurrentWorkerCount);

    for (std::size_t idx = 0U; idx < kConcurrentWorkerCount; ++idx) {
        std::string const spec_str = "gatewayd/concurrent_local_" + std::to_string(idx);
        ServiceConfigParams params{};
        params.local_specifier = spec_str;
        params.instance_id = static_cast<std::uint16_t>(kPrimaryInstanceId + idx);
        configs.push_back(BuildConfig(
            {EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize}}, params));

        auto const expected_specifier =
            score::mw::com::impl::InstanceSpecifier::Create(std::string{spec_str}).value();
        auto const fh = score::mw::com::impl::make_FindServiceHandle(kFindHandleUid + idx);
        find_handles.push_back(fh);
        service_handles.push_back(MakeHandle(spec_str, {kEventSerialized}, params.instance_id));

        EXPECT_CALL(service_discovery_mock(), StartFindService(_, expected_specifier))
            .WillOnce([&handlers, idx, fh](DiscoveryHandler handler,
                                           score::mw::com::impl::InstanceSpecifier) {
                handlers[idx] = std::move(handler);
                return score::Result<score::mw::com::impl::FindServiceHandle>{fh};
            });
        EXPECT_CALL(service_discovery_mock(), StopFindService(fh))
            .WillOnce(Return(score::Result<void>{}));

        (void)ExpectProxyWithEvent(kEventSerialized);
        (void)ExpectServerConnectorCreated();
    }

    std::vector<std::unique_ptr<LocalServiceInstance>> shared_instances{};
    for (std::size_t idx = 0U; idx < kConcurrentWorkerCount; ++idx) {
        auto const res = LocalServiceInstance::CreateAsyncLocalServices(
            configs[idx].local_instance, configs[idx].service_type, socom_runtime_,
            shared_instances);
        ASSERT_TRUE(res.has_value());
    }

    std::promise<void> start_promise{};
    std::shared_future<void> const start_gate{start_promise.get_future()};
    std::atomic<std::size_t> ready_workers{0U};

    std::vector<std::thread> workers{};
    workers.reserve(kConcurrentWorkerCount);
    for (std::size_t idx = 0U; idx < kConcurrentWorkerCount; ++idx) {
        workers.emplace_back(
            [&handlers, &service_handles, &find_handles, &ready_workers, start_gate, idx]() {
                (void)ready_workers.fetch_add(1U, std::memory_order_acq_rel);
                start_gate.wait();
                handlers[idx]({service_handles[idx]}, find_handles[idx]);
                handlers[idx]({service_handles[idx]}, find_handles[idx]);
            });
    }

    while (ready_workers.load(std::memory_order_acquire) < kConcurrentWorkerCount) {
        std::this_thread::yield();
    }
    start_promise.set_value();

    for (auto& worker : workers) {
        worker.join();
    }

    ASSERT_EQ(shared_instances.size(), kConcurrentWorkerCount);
    for (auto const& instance : shared_instances) {
        EXPECT_NE(instance, nullptr);
    }
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest, DISABLED_CreateAsyncIgnoresEmptyHandleNotifications) {
    auto const expected_specifier =
        score::mw::com::impl::InstanceSpecifier::Create(std::string{kLocalSpecifier}).value();
    auto const find_handle = score::mw::com::impl::make_FindServiceHandle(kFindHandleUid);
    DiscoveryHandler captured_handler{};

    EXPECT_CALL(service_discovery_mock(), StartFindService(_, expected_specifier))
        .WillOnce([&captured_handler, find_handle](DiscoveryHandler handler,
                                                   score::mw::com::impl::InstanceSpecifier) {
            captured_handler = std::move(handler);
            return score::Result<score::mw::com::impl::FindServiceHandle>{find_handle};
        });
    EXPECT_CALL(service_discovery_mock(), StopFindService(_)).Times(0);
    EXPECT_CALL(proxy_binding_factory_mock(), Create(_)).Times(0);

    std::vector<std::unique_ptr<LocalServiceInstance>> instances{};
    auto const start_result = LocalServiceInstance::CreateAsyncLocalServices(
        default_cfg_.local_instance, default_cfg_.service_type, socom_runtime_, instances);
    ASSERT_TRUE(start_result.has_value());

    captured_handler({}, find_handle);
    EXPECT_EQ(instances.size(), kExpectedZeroInstances);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest, DISABLED_DropsUndersizedWritablePayloadsWithoutWritingBuffer) {
    struct UndersizedCase {
        const char* label;
        bool null_span;
        std::size_t writable_size;
    };

    constexpr std::array<UndersizedCase, kUndersizedCaseCount> kUndersizedCases{{
        {"empty_null_span_0_bytes", true, kZeroBytes},
        {"non_null_0_bytes", false, kZeroBytes},
        {"non_null_1_byte", false, kOneByte},
        {"non_null_15_bytes", false, kUndersizedFifteenBytes},
    }};

    std::array<serializer::PreSerializedData<kMaxMessageSize>, kUndersizedCaseCount> samples{{
        MakePreSerializedSample<kMaxMessageSize>({0x01U, 0x02U}),
        MakePreSerializedSample<kMaxMessageSize>({0x01U, 0x02U}),
        MakePreSerializedSample<kMaxMessageSize>({0x01U, 0x02U}),
        MakePreSerializedSample<kMaxMessageSize>({0x01U, 0x02U}),
    }};
    std::array<CanaryBackingBuffer, kUndersizedCaseCount> canaries{{
        CanaryBackingBuffer{kStandardPayloadBufferCapacity, kCanarySentinelByte},
        CanaryBackingBuffer{kStandardPayloadBufferCapacity, kCanarySentinelByte},
        CanaryBackingBuffer{kStandardPayloadBufferCapacity, kCanarySentinelByte},
        CanaryBackingBuffer{kStandardPayloadBufferCapacity, kCanarySentinelByte},
    }};
    ReceiveHandlerWeakPtr handler{};

    auto const plan = PrepareSingleEventProxy(kLocalSpecifier, kEventSerialized);
    ASSERT_NE(plan.event_mock, nullptr);
    CaptureReceiveHandler(*plan.event_mock, handler);

    auto const make_sample_delivery = [](auto& sample) {
        return [&sample](auto&& callback, score::mw::com::impl::TrackerGuardFactory& tracker) {
            InvokeSampleCallback(callback, score::testing::MakeMockSample(&sample, tracker));
            return score::Result<std::size_t>{kOneByte};
        };
    };
    EXPECT_CALL(*plan.event_mock, GetNewSamples(_, _))
        .WillOnce(make_sample_delivery(samples[0]))
        .WillOnce(make_sample_delivery(samples[1]))
        .WillOnce(make_sample_delivery(samples[2]))
        .WillOnce(make_sample_delivery(samples[3]));

    auto* const connector_ptr = ExpectServerConnectorCreated();
    ASSERT_NE(connector_ptr, nullptr);
    auto const make_undersized_alloc = [](CanaryBackingBuffer& canary,
                                          UndersizedCase const test_case) {
        return [&canary, test_case](socom::Event_id) noexcept {
            if (test_case.null_span) {
                return socom::Writable_payload{socom::Payload::Writable_span{}};
            }
            return canary.MakePayload(test_case.writable_size);
        };
    };
    EXPECT_CALL(*connector_ptr, allocate_event_payload(kPositionalEventId0))
        .WillOnce(make_undersized_alloc(canaries[0], kUndersizedCases[0]))
        .WillOnce(make_undersized_alloc(canaries[1], kUndersizedCases[1]))
        .WillOnce(make_undersized_alloc(canaries[2], kUndersizedCases[2]))
        .WillOnce(make_undersized_alloc(canaries[3], kUndersizedCases[3]));
    EXPECT_CALL(*connector_ptr, update_event(_, _)).Times(0);

    auto proxy_result = score::mw::com::GenericProxy::Create(plan.handle);
    ASSERT_TRUE(proxy_result.has_value());

    auto instance_result =
        LocalServiceInstance::Create(default_cfg_.local_instance, default_cfg_.service_type,
                                     std::move(proxy_result).value(), socom_runtime_);
    ASSERT_TRUE(instance_result.has_value());

    for (std::size_t idx = 0U; idx < kUndersizedCaseCount; ++idx) {
        SCOPED_TRACE(kUndersizedCases[idx].label);
        ASSERT_TRUE(TriggerReceiveHandler(handler));
        EXPECT_TRUE(canaries[idx].IsUnmodified(kCanarySentinelByte));
    }
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(LocalServiceInstanceTest, DISABLED_CreateRejectsNullAndIncompleteConfigurations) {
    struct InvalidCreateCase {
        const char* label;
        MalformedConfigKind kind;
    };

    constexpr std::array<InvalidCreateCase, kInvalidCreateCaseCount> kCases{{
        {"null_service_instance_config", MalformedConfigKind::kNullServiceInstance},
        {"null_service_type_config", MalformedConfigKind::kNullServiceType},
        {"missing_service_type_name", MalformedConfigKind::kMissingServiceTypeNameField},
        {"null_events_vector", MalformedConfigKind::kMissingEventsVectorField},
        {"missing_event_name", MalformedConfigKind::kMissingEventNameField},
    }};

    std::vector<BuiltConfig> configs{};
    configs.reserve(kInvalidCreateCaseCount);
    std::vector<SingleEventProxyPlan> plans{};
    plans.reserve(kInvalidCreateCaseCount);

    EXPECT_CALL(socom_runtime_, make_server_connector(_, _, _)).Times(0);

    for (auto const& test_case : kCases) {
        configs.push_back(BuildRawConfigVariant(test_case.kind));
        auto plan = PrepareSingleEventProxy(kLocalSpecifier, kEventSerialized);
        ASSERT_NE(plan.event_mock, nullptr);
        EXPECT_CALL(*plan.event_mock, SetReceiveHandler(_)).Times(0);
        EXPECT_CALL(*plan.event_mock, Subscribe(_)).Times(0);
        plans.push_back(std::move(plan));
    }

    ASSERT_EQ(plans.size(), kInvalidCreateCaseCount);
    for (std::size_t idx = 0U; idx < kInvalidCreateCaseCount; ++idx) {
        SCOPED_TRACE(kCases[idx].label);
        auto proxy_result = score::mw::com::GenericProxy::Create(plans[idx].handle);
        ASSERT_TRUE(proxy_result.has_value());

        auto result =
            LocalServiceInstance::Create(configs[idx].local_instance, configs[idx].service_type,
                                         std::move(proxy_result).value(), socom_runtime_);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), score::mw::com::ComErrc::kInvalidConfiguration);
    }
}

}  // namespace
}  // namespace score::someip_gateway::gatewayd
