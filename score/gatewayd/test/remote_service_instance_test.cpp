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

#include "score/gatewayd/impl/remote_service_instance.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "flatbuffers/flatbuffers.h"
#include "score/config/mw_someip_config_generated.h"
#include "score/gatewayd/test/service_instance_test_fixture.h"
#include "score/mw/com/com_error_domain.h"
#include "score/mw/com/types.h"
#include "score/serializer/pre_serialized_data.h"
#include "score/serializer/serializer.h"
#include "score/socom/client_connector.hpp"
#include "score/socom/error.hpp"
#include "score/socom/event.hpp"
#include "score/socom/payload.hpp"
#include "score/socom/service_interface_definition.hpp"
#include "score/socom/service_interface_identifier.hpp"
#include "score/someip/constants.h"
#include "score/test/mw_com_mocks.hpp"

namespace score::someip_gateway::gatewayd {
namespace {

constexpr socom::Event_id kEventIndex0{0U};
constexpr socom::Event_id kEventIndex1{1U};
constexpr socom::Event_id kEventIndex2{2U};
constexpr socom::Event_id kUnknownEventId{99U};
constexpr std::string_view kUnresolvedRemoteSpecifier{"gatewayd/unresolved_remote"};

constexpr std::uint8_t kSignBitMajorVersion{0x80U};
constexpr std::uint32_t kSignBitMinorVersion{0x000080FFU};
constexpr std::uint16_t kExpectedTruncatedMinorVersion{0x80FFU};

constexpr std::uint8_t kSomeipServiceIdMsb{0x12U};
constexpr std::uint8_t kSomeipServiceIdLsb{0x34U};
constexpr std::uint8_t kSomeipEventIdMsb{0x80U};
constexpr std::uint8_t kSomeipEventIdLsb{0x01U};
constexpr std::uint8_t kSomeipZeroByteLengthLsb{0x08U};
constexpr std::uint8_t kSomeipFourByteLengthLsb{0x0CU};
constexpr std::uint8_t kSomeipMaxMinusOneLengthLsb{0x27U};
constexpr std::uint8_t kSomeipMaxLengthLsb{0x28U};
constexpr std::uint8_t kSomeipMaxPlusOneLengthLsb{0x29U};
constexpr std::uint8_t kSomeipClientIdByte{0xFFU};
constexpr std::uint8_t kSomeipSessionIdLsb{0x01U};
constexpr std::uint8_t kSomeipProtocolVersion{0x01U};
constexpr std::uint8_t kSomeipInterfaceVersion{0x01U};
constexpr std::uint8_t kSomeipNotificationType{0x02U};
constexpr std::uint8_t kSomeipReturnOk{0x00U};

constexpr std::size_t kZeroBytes{0U};
constexpr std::size_t kTruncatedOneByteSize{1U};
constexpr std::size_t kTruncatedFifteenByteSize{someip::kSomeipFullHeaderSize - 1U};
constexpr std::size_t kFourBytePayloadSize{4U};
constexpr std::size_t kMaxMinusOnePayloadSize{kMaxMessageSize - 1U};
constexpr std::size_t kMaxPlusOnePayloadSize{kMaxMessageSize + 1U};
constexpr std::size_t kSentinelSampleSize{99U};
constexpr std::size_t kTwoSkeletonEvents{2U};
constexpr std::size_t kThreeConfiguredEvents{3U};
constexpr std::uint32_t kMissingIpcEventMaxSize{16U};
constexpr std::uint8_t kPatternBaseByte{0x70U};
constexpr std::uint8_t kRecoveryPatternBaseByte{0x30U};
constexpr std::uint8_t kOversizeFillByte{0xEEU};
constexpr std::byte kCanaryFillByte{0x5AU};

struct RawServiceTypeOptions {
    bool include_service_type_name{true};
    bool include_events_vector{true};
};

BuiltConfig BuildConfigWithRawServiceType(RawServiceTypeOptions const options) {
    flatbuffers::FlatBufferBuilder fbb{};

    auto const remote_inst = mw_someip_config::CreateServiceInstance(
        fbb, fbb.CreateString(kRemoteSpecifier.data(), kRemoteSpecifier.size()), kInstanceId);
    auto const remote_instances_offset = fbb.CreateVector(&remote_inst, kSingleElementCount);

    flatbuffers::Offset<flatbuffers::String> service_type_name_offset{};
    if (options.include_service_type_name) {
        service_type_name_offset =
            fbb.CreateString(kServiceTypeName.data(), kServiceTypeName.size());
    }

    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<mw_someip_config::Event>>>
        events_offset{};
    if (options.include_events_vector) {
        auto const null_cfg = mw_someip_config::CreateNullSerializerConfig(fbb, kMaxMessageSize);
        auto const ev = mw_someip_config::CreateEvent(
            fbb, kEventSerializedId,
            fbb.CreateString(kEventSerialized.data(), kEventSerialized.size()),
            mw_someip_config::SerializationConfig_NullSerializerConfig, null_cfg.Union());
        events_offset = fbb.CreateVector(&ev, kSingleElementCount);
    }

    auto const service_type = mw_someip_config::CreateServiceType(
        fbb, service_type_name_offset, kServiceId, kServiceMajorVersion, kServiceMinorVersion,
        events_offset, {}, {}, remote_instances_offset);
    auto const service_types_offset = fbb.CreateVector(&service_type, kSingleElementCount);
    auto const root = mw_someip_config::CreateRoot(fbb, service_types_offset);
    fbb.Finish(root);

    auto const raw_buffer = std::make_shared<std::vector<std::uint8_t>>(
        fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());
    auto const* root_ptr = mw_someip_config::GetRoot(raw_buffer->data());
    auto const* st_ptr = root_ptr->service_types()->Get(kFirstElementIndex);
    auto const* remote_ptr = st_ptr->remote_service_instances()->Get(kFirstElementIndex);

    return BuiltConfig{
        raw_buffer,
        std::shared_ptr<mw_someip_config::ServiceType const>{raw_buffer, st_ptr},
        nullptr,
        std::shared_ptr<mw_someip_config::ServiceInstance const>{raw_buffer, remote_ptr},
    };
}

constexpr std::array<std::uint8_t, someip::kSomeipFullHeaderSize> MakeSomeipHeader(
    std::uint8_t const length_lsb) noexcept {
    return {
        kSomeipServiceIdMsb,
        kSomeipServiceIdLsb,
        kSomeipEventIdMsb,
        kSomeipEventIdLsb,
        0x00U,
        0x00U,
        0x00U,
        length_lsb,
        kSomeipClientIdByte,
        kSomeipClientIdByte,
        0x00U,
        kSomeipSessionIdLsb,
        kSomeipProtocolVersion,
        kSomeipInterfaceVersion,
        kSomeipNotificationType,
        kSomeipReturnOk,
    };
}

socom::Payload MakePayloadFromBytes(score::cpp::span<const std::uint8_t> const raw_bytes) {
    std::vector<std::byte> storage(raw_bytes.size());
    std::transform(raw_bytes.begin(), raw_bytes.end(), storage.begin(),
                   [](std::uint8_t const b) noexcept { return static_cast<std::byte>(b); });
    socom::Payload::Span const span{storage.data(), storage.size()};
    return socom::Payload{span, std::move(storage)};
}

socom::Payload MakeFramedSomeipPayload(std::uint8_t const length_lsb,
                                       score::cpp::span<const std::uint8_t> const payload_bytes) {
    auto const header = MakeSomeipHeader(length_lsb);
    std::vector<std::uint8_t> frame{};
    frame.reserve(header.size() + payload_bytes.size());
    frame.insert(frame.end(), header.begin(), header.end());
    frame.insert(frame.end(), payload_bytes.begin(), payload_bytes.end());
    return MakePayloadFromBytes(frame);
}

socom::Server_service_interface_definition MakeServerDefinition(std::size_t const num_events) {
    return socom::Server_service_interface_definition{
        socom::Service_interface_identifier{
            kServiceTypeName,
            socom::Service_interface_identifier::Version{
                kServiceMajorVersion, static_cast<std::uint16_t>(kServiceMinorVersion)}},
        socom::to_num_of_methods(kZeroBytes), socom::to_num_of_events(num_events)};
}

class TrackableClientConnectorMock : public socom::Client_connector_mock {
   public:
    using DestroyHook =
        std::function<void(socom::Client_connector const&, socom::Client_connector::Callbacks&)>;

    TrackableClientConnectorMock() = default;

    explicit TrackableClientConnectorMock(DestroyHook on_destroy)
        : on_destroy_{std::move(on_destroy)} {}

    ~TrackableClientConnectorMock() noexcept override {
        if (has_callbacks_ && on_destroy_) {
            on_destroy_(*this, callbacks_);
        }
        Die();
    }

    TrackableClientConnectorMock(TrackableClientConnectorMock const&) = delete;
    TrackableClientConnectorMock& operator=(TrackableClientConnectorMock const&) = delete;
    TrackableClientConnectorMock(TrackableClientConnectorMock&&) = delete;
    TrackableClientConnectorMock& operator=(TrackableClientConnectorMock&&) = delete;

    void SetCallbacks(socom::Client_connector::Callbacks callbacks) {
        callbacks_ = std::move(callbacks);
        has_callbacks_ = true;
    }

    MOCK_METHOD(void, Die, (), ());

   private:
    socom::Client_connector::Callbacks callbacks_{};
    DestroyHook on_destroy_{};
    bool has_callbacks_{false};
};

class RemoteServiceInstanceTest : public ServiceInstanceTestFixture {
   protected:
    static constexpr std::size_t kDefaultSampleByteSize{
        serializer::get_size_of_pre_serialized_data(kMaxMessageSize)};
    static constexpr std::size_t kDefaultSampleAlignment{
        alignof(serializer::PreSerializedData<kMaxMessageSize>)};

    static score::Result<score::mw::com::GenericSkeleton> CreateGenericSkeleton(
        std::string_view const specifier,
        std::initializer_list<std::string_view> const event_names) {
        auto specifier_result = score::mw::com::InstanceSpecifier::Create(std::string{specifier});
        if (!specifier_result.has_value()) {
            return score::MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
        }

        std::vector<score::mw::com::EventInfo> events{};
        events.reserve(event_names.size());
        for (auto const name : event_names) {
            events.push_back(score::mw::com::EventInfo{
                name,
                score::mw::com::DataTypeMetaInfo{kDefaultSampleByteSize, kDefaultSampleAlignment}});
        }
        score::mw::com::GenericSkeletonServiceElementInfo element_info{};
        element_info.events =
            score::cpp::span<const score::mw::com::EventInfo>{events.data(), events.size()};
        return score::mw::com::GenericSkeleton::Create(*specifier_result, element_info);
    }
};

TEST_F(RemoteServiceInstanceTest, CreateAsyncRemoteServiceCreatesOffersAndAppendsInstance) {
    auto const cfg = BuildConfig(
        {EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize}},
        ServiceConfigParams{kServiceTypeName, kServiceId, kSignBitMajorVersion,
                            kSignBitMinorVersion, kLocalSpecifier, kRemoteSpecifier, kInstanceId});
    ReinitSerializer(cfg);
    MwComTestFixture::StubInstanceResolved(
        kRemoteSpecifier, {kEventSerialized},
        score::testing::ServiceDeploymentSpec{kServiceTypeName, kServiceId, kSignBitMajorVersion,
                                              kSignBitMinorVersion, kInstanceId,
                                              kDefaultGatewaydSampleSlots});

    auto skel_mock = std::make_unique<::testing::NiceMock<score::testing::Skeleton>>();
    auto* const skel_ptr = skel_mock.get();
    auto event_mock = std::make_unique<::testing::NiceMock<score::testing::GenericSkeletonEvent>>();
    auto* const event_ptr = event_mock.get();

    EXPECT_CALL(skeleton_binding_factory_mock(), Create(::testing::_))
        .InSequence(skeleton_factory_sequence())
        .WillOnce(::testing::Return(::testing::ByMove(std::move(skel_mock))))
        .RetiresOnSaturation();

    std::optional<score::memory::DataTypeSizeInfo> captured_size_info{};
    EXPECT_CALL(generic_skeleton_event_factory_mock(),
                Create(::testing::_, ::testing::Ref(*skel_ptr), kEventSerialized, ::testing::_))
        .WillOnce([&captured_size_info, ev = std::move(event_mock)](
                      score::mw::com::impl::InstanceIdentifier const&,
                      score::mw::com::impl::SkeletonBinding&, std::string_view,
                      score::memory::DataTypeSizeInfo const size_info) mutable
                      -> std::unique_ptr<score::mw::com::impl::SkeletonEventBinding> {
            captured_size_info.emplace(size_info);
            return std::move(ev);
        });

    EXPECT_CALL(service_discovery_mock(), OfferService(::testing::_))
        .WillOnce(::testing::Return(score::Result<void>{}));

    auto cc_mock = std::make_unique<::testing::NiceMock<socom::Client_connector_mock>>();
    auto* const cc_ptr = cc_mock.get();
    std::optional<socom::Service_interface_definition> captured_def{};
    std::optional<socom::Service_instance> captured_inst{};
    socom::Client_connector::Callbacks callbacks{};
    EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
        .InSequence(client_connector_sequence())
        .WillOnce([&captured_def, &captured_inst, &callbacks, cc = std::move(cc_mock)](
                      socom::Service_interface_definition def, socom::Service_instance inst,
                      socom::Client_connector::Callbacks cbs) mutable
                      -> score::Result<socom::Client_connector::Uptr> {
            captured_def.emplace(std::move(def));
            captured_inst.emplace(std::move(inst));
            callbacks = std::move(cbs);
            return std::move(cc);
        });

    EXPECT_CALL(*event_ptr, Allocate(::testing::_)).Times(0);
    EXPECT_CALL(*event_ptr, Send(::testing::_, ::testing::_)).Times(0);

    std::vector<std::unique_ptr<RemoteServiceInstance>> instances{};
    auto const result = RemoteServiceInstance::CreateAsyncRemoteService(
        cfg.remote_instance, cfg.service_type, socom_runtime_, instances);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(instances.size(), kSingleElementCount);
    ASSERT_NE(instances.front(), nullptr);

    ASSERT_TRUE(captured_def.has_value());
    EXPECT_EQ(captured_def->interface.id.StringView(), kServiceTypeName);
    EXPECT_EQ(captured_def->interface.version.major,
              static_cast<std::uint16_t>(kSignBitMajorVersion));
    EXPECT_EQ(captured_def->interface.version.minor, kExpectedTruncatedMinorVersion);
    EXPECT_EQ(captured_def->num_methods, kZeroBytes);
    EXPECT_EQ(captured_def->num_events, kSingleElementCount);

    ASSERT_TRUE(captured_inst.has_value());
    EXPECT_EQ(*captured_inst, socom::Service_instance{kServiceTypeName});

    ASSERT_TRUE(captured_size_info.has_value());
    EXPECT_EQ(captured_size_info->Size(), kDefaultSampleByteSize);
    EXPECT_EQ(captured_size_info->Alignment(), kDefaultSampleAlignment);

    ASSERT_FALSE(callbacks.on_service_state_change.empty());
    ASSERT_FALSE(callbacks.on_event_update.empty());
    ASSERT_FALSE(callbacks.on_event_requested_update.empty());
    ASSERT_FALSE(callbacks.on_event_payload_allocate.empty());

    std::array<std::uint8_t, kFourBytePayloadSize> const dummy_payload{0x01U, 0x02U, 0x03U, 0x04U};
    callbacks.on_event_requested_update(
        *cc_ptr, kEventIndex0, MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, dummy_payload));
}

TEST_F(RemoteServiceInstanceTest, HandlesSynchronousAvailableCallbackDuringConnectorCreation) {
    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    auto const skel_mocks = ExpectSkeletonWithEvent(kEventSerialized);
    (void)skel_mocks;

    EXPECT_CALL(service_discovery_mock(), OfferService(::testing::_))
        .WillOnce(::testing::Return(score::Result<void>{}));

    auto cc_mock = std::make_unique<::testing::NiceMock<socom::Client_connector_mock>>();
    auto* const cc_ptr = cc_mock.get();
    EXPECT_CALL(*cc_ptr, subscribe_event(kEventIndex0, socom::Event_mode::update))
        .WillOnce(::testing::Return(score::Result<void>{}));

    EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
        .InSequence(client_connector_sequence())
        .WillOnce([cc = std::move(cc_mock)](socom::Service_interface_definition,
                                            socom::Service_instance,
                                            socom::Client_connector::Callbacks callbacks) mutable
                      -> score::Result<socom::Client_connector::Uptr> {
            auto const server_def = MakeServerDefinition(kSingleElementCount);
            callbacks.on_service_state_change(*cc, socom::Service_state::available, server_def);
            return std::move(cc);
        });

    auto skeleton_res = CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized});
    ASSERT_TRUE(skeleton_res.has_value());

    auto result =
        RemoteServiceInstance::Create(default_cfg_.remote_instance, default_cfg_.service_type,
                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(*result, nullptr);
}

TEST_F(RemoteServiceInstanceTest, ForwardsZeroBytePayloadOnHeaderOnlyFrame) {
    serializer::PreSerializedData<kMaxMessageSize> sample_slot{};
    sample_slot.size = kSentinelSampleSize;
    void const* sent_ptr{nullptr};
    socom::Client_connector::Callbacks callbacks{};

    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    auto const skel_mocks = ExpectSkeletonWithEvent(kEventSerialized);
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    EXPECT_CALL(*skel_mocks.event, Allocate(::testing::_))
        .WillOnce([&sample_slot](score::mw::com::impl::SampleAllocateeGuard guard) {
            return score::testing::MakeAllocatedSample(&sample_slot, std::move(guard));
        });

    EXPECT_CALL(*skel_mocks.event, Send(::testing::_, ::testing::_))
        .WillOnce(
            [&sent_ptr](
                score::mw::com::impl::SampleAllocateePtr<void> sample,
                std::optional<score::mw::com::impl::SkeletonEventBinding::SendTraceCallback>) {
                sent_ptr = sample.Get();
                return score::Result<void>{};
            });

    auto skeleton_res = CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized});
    ASSERT_TRUE(skeleton_res.has_value());

    auto instance_res =
        RemoteServiceInstance::Create(default_cfg_.remote_instance, default_cfg_.service_type,
                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_TRUE(instance_res.has_value());

    auto const header_only = MakeSomeipHeader(kSomeipZeroByteLengthLsb);
    callbacks.on_event_update(*cc_ptr, kEventIndex0, MakePayloadFromBytes(header_only));

    EXPECT_EQ(sent_ptr, &sample_slot);
    EXPECT_EQ(sample_slot.size, kZeroBytes);
}

TEST_F(RemoteServiceInstanceTest, StripsHeaderAndDeserializesSignBitBoundaryPayload) {
    constexpr std::array<std::uint8_t, someip::kSomeipFullHeaderSize + kFourBytePayloadSize>
        kCanonicalWireFrame{
            0x12U, 0x34U, 0x80U, 0x01U, 0x00U, 0x00U, 0x00U, 0x0CU, 0xFFU, 0xFFU,
            0x00U, 0x01U, 0x01U, 0x01U, 0x02U, 0x00U, 0x00U, 0x7FU, 0x80U, 0xFFU,
    };
    constexpr std::array<std::byte, kFourBytePayloadSize> kExpectedPayload{
        std::byte{0x00U}, std::byte{0x7FU}, std::byte{0x80U}, std::byte{0xFFU}};

    serializer::PreSerializedData<kMaxMessageSize> sample_slot{};
    std::fill(std::begin(sample_slot.data), std::end(sample_slot.data), kCanaryFillByte);
    void const* sent_ptr{nullptr};
    socom::Client_connector::Callbacks callbacks{};

    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    auto const skel_mocks = ExpectSkeletonWithEvent(kEventSerialized);
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    EXPECT_CALL(*skel_mocks.event, Allocate(::testing::_))
        .WillOnce([&sample_slot](score::mw::com::impl::SampleAllocateeGuard guard) {
            return score::testing::MakeAllocatedSample(&sample_slot, std::move(guard));
        });

    EXPECT_CALL(*skel_mocks.event, Send(::testing::_, ::testing::_))
        .WillOnce(
            [&sent_ptr](
                score::mw::com::impl::SampleAllocateePtr<void> sample,
                std::optional<score::mw::com::impl::SkeletonEventBinding::SendTraceCallback>) {
                sent_ptr = sample.Get();
                return score::Result<void>{};
            });

    auto skeleton_res = CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized});
    ASSERT_TRUE(skeleton_res.has_value());

    auto instance_res =
        RemoteServiceInstance::Create(default_cfg_.remote_instance, default_cfg_.service_type,
                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_TRUE(instance_res.has_value());

    callbacks.on_event_update(*cc_ptr, kEventIndex0, MakePayloadFromBytes(kCanonicalWireFrame));

    EXPECT_EQ(sent_ptr, &sample_slot);
    ASSERT_EQ(sample_slot.size, kFourBytePayloadSize);
    score::cpp::span<const std::byte> const actual_payload{sample_slot.data, sample_slot.size};
    EXPECT_THAT(actual_payload, ::testing::ElementsAreArray(kExpectedPayload));
}

TEST_F(RemoteServiceInstanceTest, EnforcesMaxMessageSizeBoundaryOnInboundFrames) {
    std::array<std::uint8_t, kMaxMessageSize> exact_max_bytes{};
    std::array<std::byte, kMaxMessageSize> expected_max_bytes{};
    for (std::size_t i{0U}; i < kMaxMessageSize; ++i) {
        auto const val = static_cast<std::uint8_t>(kPatternBaseByte + i);
        exact_max_bytes[i] = val;
        expected_max_bytes[i] = static_cast<std::byte>(val);
    }

    std::array<std::uint8_t, kMaxPlusOnePayloadSize> oversize_bytes{};
    std::fill(oversize_bytes.begin(), oversize_bytes.end(), kOversizeFillByte);

    std::array<std::uint8_t, kMaxMinusOnePayloadSize> max_minus_one_bytes{};
    std::array<std::byte, kMaxMinusOnePayloadSize> expected_minus_one_bytes{};
    for (std::size_t i{0U}; i < kMaxMinusOnePayloadSize; ++i) {
        auto const val = static_cast<std::uint8_t>(kRecoveryPatternBaseByte + i);
        max_minus_one_bytes[i] = val;
        expected_minus_one_bytes[i] = static_cast<std::byte>(val);
    }

    serializer::PreSerializedData<kMaxMessageSize> sample_slot{};
    std::vector<std::byte> first_sent_bytes{};
    std::vector<std::byte> second_sent_bytes{};
    socom::Client_connector::Callbacks callbacks{};
    ::testing::MockFunction<void(std::string_view)> checkpoint{};

    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    auto const skel_mocks = ExpectSkeletonWithEvent(kEventSerialized);
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    ON_CALL(*skel_mocks.event, Allocate(::testing::_))
        .WillByDefault([&sample_slot](score::mw::com::impl::SampleAllocateeGuard guard) {
            return score::testing::MakeAllocatedSample(&sample_slot, std::move(guard));
        });

    {
        ::testing::InSequence send_order{};
        EXPECT_CALL(*skel_mocks.event, Send(::testing::_, ::testing::_))
            .WillOnce(
                [&first_sent_bytes](
                    score::mw::com::impl::SampleAllocateePtr<void> sample,
                    std::optional<score::mw::com::impl::SkeletonEventBinding::SendTraceCallback>) {
                    auto const* typed =
                        static_cast<const serializer::PreSerializedData<kMaxMessageSize>*>(
                            sample.Get());
                    first_sent_bytes.assign(typed->data, typed->data + typed->size);
                    return score::Result<void>{};
                });
        EXPECT_CALL(checkpoint, Call(std::string_view{"after_max_size"})).Times(1);
        EXPECT_CALL(checkpoint, Call(std::string_view{"after_oversize"})).Times(1);
        EXPECT_CALL(*skel_mocks.event, Send(::testing::_, ::testing::_))
            .WillOnce(
                [&second_sent_bytes](
                    score::mw::com::impl::SampleAllocateePtr<void> sample,
                    std::optional<score::mw::com::impl::SkeletonEventBinding::SendTraceCallback>) {
                    auto const* typed =
                        static_cast<const serializer::PreSerializedData<kMaxMessageSize>*>(
                            sample.Get());
                    second_sent_bytes.assign(typed->data, typed->data + typed->size);
                    return score::Result<void>{};
                });
    }

    auto skeleton_res = CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized});
    ASSERT_TRUE(skeleton_res.has_value());

    auto instance_res =
        RemoteServiceInstance::Create(default_cfg_.remote_instance, default_cfg_.service_type,
                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_TRUE(instance_res.has_value());

    callbacks.on_event_update(*cc_ptr, kEventIndex0,
                              MakeFramedSomeipPayload(kSomeipMaxLengthLsb, exact_max_bytes));
    checkpoint.Call("after_max_size");

    callbacks.on_event_update(*cc_ptr, kEventIndex0,
                              MakeFramedSomeipPayload(kSomeipMaxPlusOneLengthLsb, oversize_bytes));
    checkpoint.Call("after_oversize");

    callbacks.on_event_update(
        *cc_ptr, kEventIndex0,
        MakeFramedSomeipPayload(kSomeipMaxMinusOneLengthLsb, max_minus_one_bytes));

    ASSERT_EQ(first_sent_bytes.size(), kMaxMessageSize);
    EXPECT_THAT(first_sent_bytes, ::testing::ElementsAreArray(expected_max_bytes));
    ASSERT_EQ(second_sent_bytes.size(), kMaxMinusOnePayloadSize);
    EXPECT_THAT(second_sent_bytes, ::testing::ElementsAreArray(expected_minus_one_bytes));
}

TEST_F(RemoteServiceInstanceTest, DropsFrameWhenSampleAllocationFailsAndRecovers) {
    serializer::PreSerializedData<kMaxMessageSize> sample_slot{};
    constexpr std::array<std::uint8_t, kFourBytePayloadSize> kValidPayload{0x10U, 0x20U, 0x30U,
                                                                           0x40U};
    constexpr std::array<std::byte, kFourBytePayloadSize> kExpectedBytes{
        std::byte{0x10U}, std::byte{0x20U}, std::byte{0x30U}, std::byte{0x40U}};
    socom::Client_connector::Callbacks callbacks{};
    ::testing::MockFunction<void(std::string_view)> checkpoint{};

    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    auto const skel_mocks = ExpectSkeletonWithEvent(kEventSerialized);
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    {
        ::testing::InSequence order{};
        EXPECT_CALL(*skel_mocks.event, Allocate(::testing::_))
            .WillOnce(::testing::Return(
                score::MakeUnexpected(score::mw::com::impl::ComErrc::kSampleAllocationFailure)));
        EXPECT_CALL(checkpoint, Call(std::string_view{"after_alloc_error"})).Times(1);
        EXPECT_CALL(*skel_mocks.event, Allocate(::testing::_))
            .WillOnce([](score::mw::com::impl::SampleAllocateeGuard guard) {
                return score::testing::MakeAllocatedSample(nullptr, std::move(guard));
            });
        EXPECT_CALL(checkpoint, Call(std::string_view{"after_null_sample"})).Times(1);
        EXPECT_CALL(*skel_mocks.event, Allocate(::testing::_))
            .WillOnce([&sample_slot](score::mw::com::impl::SampleAllocateeGuard guard) {
                return score::testing::MakeAllocatedSample(&sample_slot, std::move(guard));
            });
        EXPECT_CALL(*skel_mocks.event, Send(::testing::_, ::testing::_))
            .WillOnce(::testing::Return(score::Result<void>{}));
    }

    auto skeleton_res = CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized});
    ASSERT_TRUE(skeleton_res.has_value());

    auto instance_res =
        RemoteServiceInstance::Create(default_cfg_.remote_instance, default_cfg_.service_type,
                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_TRUE(instance_res.has_value());

    callbacks.on_event_update(*cc_ptr, kEventIndex0,
                              MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, kValidPayload));
    checkpoint.Call("after_alloc_error");

    callbacks.on_event_update(*cc_ptr, kEventIndex0,
                              MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, kValidPayload));
    checkpoint.Call("after_null_sample");

    callbacks.on_event_update(*cc_ptr, kEventIndex0,
                              MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, kValidPayload));

    ASSERT_EQ(sample_slot.size, kFourBytePayloadSize);
    score::cpp::span<const std::byte> const actual_payload{sample_slot.data, sample_slot.size};
    EXPECT_THAT(actual_payload, ::testing::ElementsAreArray(kExpectedBytes));
}

TEST_F(RemoteServiceInstanceTest, CreateReturnsBindingFailureWhenOfferServiceFails) {
    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    auto const skel_mocks = ExpectSkeletonWithEvent(kEventSerialized);
    (void)skel_mocks;

    EXPECT_CALL(service_discovery_mock(), OfferService(::testing::_))
        .WillOnce(::testing::Return(
            score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
        .Times(0);

    auto skeleton_res = CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized});
    ASSERT_TRUE(skeleton_res.has_value());

    auto result =
        RemoteServiceInstance::Create(default_cfg_.remote_instance, default_cfg_.service_type,
                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), score::mw::com::ComErrc::kBindingFailure);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(RemoteServiceInstanceTest, DISABLED_CreateWithdrawsOfferWhenClientConnectorCreationFails) {
    ::testing::MockFunction<void()> after_create_returned{};
    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    auto const skel_mocks = ExpectSkeletonWithEvent(kEventSerialized);

    {
        ::testing::InSequence rollback_order{};
        EXPECT_CALL(service_discovery_mock(), OfferService(::testing::_))
            .WillOnce(::testing::Return(score::Result<void>{}));
        EXPECT_CALL(*skel_mocks.skeleton, PrepareStopOffer(::testing::_)).Times(1);
        EXPECT_CALL(after_create_returned, Call()).Times(1);
    }
    ExpectClientConnectorCreationFailure(socom::Error::runtime_error_request_rejected);

    auto skeleton_res = CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized});
    ASSERT_TRUE(skeleton_res.has_value());

    auto result =
        RemoteServiceInstance::Create(default_cfg_.remote_instance, default_cfg_.service_type,
                                      std::move(*skeleton_res), socom_runtime_);
    after_create_returned.Call();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), socom::Error::runtime_error_request_rejected);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(RemoteServiceInstanceTest,
       DISABLED_CreateAsyncRemoteServiceReturnsBindingFailureOnOfferOrConnectorFailure) {
    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    std::vector<std::unique_ptr<RemoteServiceInstance>> instances{};

    auto const offer_fail_mocks = ExpectSkeletonWithEvent(kEventSerialized);
    (void)offer_fail_mocks;
    auto const connector_fail_mocks = ExpectSkeletonWithEvent(kEventSerialized);

    ::testing::MockFunction<void()> after_offer_failure{};
    {
        ::testing::InSequence discovery_order{};
        EXPECT_CALL(service_discovery_mock(), OfferService(::testing::_))
            .WillOnce(::testing::Return(
                score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
        EXPECT_CALL(after_offer_failure, Call()).Times(1);
        EXPECT_CALL(service_discovery_mock(), OfferService(::testing::_))
            .WillOnce(::testing::Return(score::Result<void>{}));
        EXPECT_CALL(*connector_fail_mocks.skeleton, PrepareStopOffer(::testing::_)).Times(1);
    }
    ExpectClientConnectorCreationFailure(socom::Error::runtime_error_request_rejected);

    auto const offer_fail = RemoteServiceInstance::CreateAsyncRemoteService(
        default_cfg_.remote_instance, default_cfg_.service_type, socom_runtime_, instances);
    after_offer_failure.Call();
    ASSERT_FALSE(offer_fail.has_value());
    EXPECT_EQ(offer_fail.error(), score::mw::com::ComErrc::kBindingFailure);
    EXPECT_TRUE(instances.empty());

    auto const connector_fail = RemoteServiceInstance::CreateAsyncRemoteService(
        default_cfg_.remote_instance, default_cfg_.service_type, socom_runtime_, instances);
    ASSERT_FALSE(connector_fail.has_value());
    EXPECT_EQ(connector_fail.error(), score::mw::com::ComErrc::kBindingFailure);
    EXPECT_TRUE(instances.empty());
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(RemoteServiceInstanceTest, DISABLED_CreateRejectsEmptyEventsVector) {
    auto const empty_events_cfg = BuildConfig({});
    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    auto const skel_mocks = ExpectSkeletonWithEvent(kEventSerialized);
    EXPECT_CALL(*skel_mocks.skeleton, PrepareOffer(::testing::_, ::testing::_, ::testing::_))
        .Times(0);
    EXPECT_CALL(service_discovery_mock(), OfferService(::testing::_)).Times(0);
    EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
        .Times(0);

    auto skeleton_res = CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized});
    ASSERT_TRUE(skeleton_res.has_value());

    auto const result = RemoteServiceInstance::Create(empty_events_cfg.remote_instance,
                                                      empty_events_cfg.service_type,
                                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), score::mw::com::ComErrc::kInvalidConfiguration);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(RemoteServiceInstanceTest,
       DISABLED_CreateAsyncRemoteServiceRejectsInvalidSemanticConfiguration) {
    auto const empty_events_cfg = BuildConfig({});
    auto const unresolved_specifier_cfg =
        BuildConfig({EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize}},
                    ServiceConfigParams{kServiceTypeName, kServiceId, kServiceMajorVersion,
                                        kServiceMinorVersion, kLocalSpecifier,
                                        kUnresolvedRemoteSpecifier, kInstanceId});
    auto const empty_specifier_cfg =
        BuildConfig({EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize}},
                    ServiceConfigParams{kServiceTypeName, kServiceId, kServiceMajorVersion,
                                        kServiceMinorVersion, kLocalSpecifier, "", kInstanceId});
    auto const malformed_specifier_cfg =
        BuildConfig({EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize}},
                    ServiceConfigParams{kServiceTypeName, kServiceId, kServiceMajorVersion,
                                        kServiceMinorVersion, kLocalSpecifier, "invalid//specifier",
                                        kInstanceId});
    auto const missing_serializer_cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
        EventSpec{kEventNoSerializer, kEventNoSerializerId, std::nullopt},
    });

    struct Case {
        std::string_view label;
        std::shared_ptr<const mw_someip_config::ServiceInstance> instance_cfg;
        std::shared_ptr<const mw_someip_config::ServiceType> type_cfg;
    };

    std::array<Case, 5U> const cases{{
        {"empty_events_vector", empty_events_cfg.remote_instance, empty_events_cfg.service_type},
        {"missing_event_serializer", missing_serializer_cfg.remote_instance,
         missing_serializer_cfg.service_type},
        {"unresolved_instance_specifier", unresolved_specifier_cfg.remote_instance,
         unresolved_specifier_cfg.service_type},
        {"empty_instance_specifier", empty_specifier_cfg.remote_instance,
         empty_specifier_cfg.service_type},
        {"malformed_instance_specifier", malformed_specifier_cfg.remote_instance,
         malformed_specifier_cfg.service_type},
    }};

    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized, kEventNoSerializer});

    ::testing::MockFunction<void()> before_factory_failure{};
    EXPECT_CALL(before_factory_failure, Call()).Times(1).InSequence(skeleton_factory_sequence());
    ExpectSkeletonCreationFailure();

    EXPECT_CALL(service_discovery_mock(), OfferService(::testing::_)).Times(0);
    EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
        .Times(0);

    std::vector<std::unique_ptr<RemoteServiceInstance>> instances{};
    for (auto const& tc : cases) {
        SCOPED_TRACE(tc.label);
        auto const result = RemoteServiceInstance::CreateAsyncRemoteService(
            tc.instance_cfg, tc.type_cfg, socom_runtime_, instances);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), score::mw::com::ComErrc::kInvalidConfiguration);
        EXPECT_TRUE(instances.empty());
    }

    before_factory_failure.Call();
    auto const factory_fail_result = RemoteServiceInstance::CreateAsyncRemoteService(
        default_cfg_.remote_instance, default_cfg_.service_type, socom_runtime_, instances);
    ASSERT_FALSE(factory_fail_result.has_value());
    EXPECT_EQ(factory_fail_result.error(), score::mw::com::ComErrc::kInvalidConfiguration);
    EXPECT_TRUE(instances.empty());
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(RemoteServiceInstanceTest,
       DISABLED_DestructorStopsOfferingServiceAndDestroysClientConnector) {
    ::testing::MockFunction<void(std::string_view)> checkpoint{};
    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    auto const skel_mocks = ExpectSkeletonWithEvent(kEventSerialized);

    auto connector = std::make_unique<::testing::NiceMock<TrackableClientConnectorMock>>();
    auto* const connector_raw = connector.get();
    bool connector_created{false};

    EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
        .InSequence(client_connector_sequence())
        .WillOnce([&connector_created, connector_raw, cc = std::move(connector)](
                      socom::Service_interface_definition, socom::Service_instance,
                      socom::Client_connector::Callbacks callbacks) mutable
                      -> score::Result<socom::Client_connector::Uptr> {
            connector_raw->SetCallbacks(std::move(callbacks));
            connector_created = true;
            return std::move(cc);
        });

    EXPECT_CALL(service_discovery_mock(), OfferService(::testing::_))
        .WillOnce(::testing::Return(score::Result<void>{}));

    {
        ::testing::InSequence teardown_order{};
        EXPECT_CALL(checkpoint, Call(std::string_view{"before_destroy"})).Times(1);
        EXPECT_CALL(*connector_raw, Die()).Times(1);
        EXPECT_CALL(*skel_mocks.skeleton, PrepareStopOffer(::testing::_)).Times(1);
        EXPECT_CALL(checkpoint, Call(std::string_view{"after_destroy"})).Times(1);
    }

    auto skeleton_res = CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized});
    ASSERT_TRUE(skeleton_res.has_value());

    auto instance_res =
        RemoteServiceInstance::Create(default_cfg_.remote_instance, default_cfg_.service_type,
                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_TRUE(instance_res.has_value());
    ASSERT_TRUE(connector_created);

    checkpoint.Call("before_destroy");
    instance_res->reset();
    checkpoint.Call("after_destroy");
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(RemoteServiceInstanceTest, DISABLED_DropsTruncatedFramesAndRecoversOnSubsequentValidFrame) {
    serializer::PreSerializedData<kMaxMessageSize> sample_slot{};
    constexpr std::array<std::uint8_t, kFourBytePayloadSize> kFirstPayload{0x11U, 0x22U, 0x33U,
                                                                           0x44U};
    constexpr std::array<std::byte, kFourBytePayloadSize> kFirstExpected{
        std::byte{0x11U}, std::byte{0x22U}, std::byte{0x33U}, std::byte{0x44U}};
    constexpr std::array<std::uint8_t, kFourBytePayloadSize> kSecondPayload{0xCAU, 0xFEU, 0xBAU,
                                                                            0xBEU};
    constexpr std::array<std::byte, kFourBytePayloadSize> kSecondExpected{
        std::byte{0xCAU}, std::byte{0xFEU}, std::byte{0xBAU}, std::byte{0xBEU}};

    std::vector<std::byte> first_received{};
    std::vector<std::byte> second_received{};
    socom::Client_connector::Callbacks callbacks{};
    ::testing::MockFunction<void(std::string_view)> checkpoint{};

    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    auto const skel_mocks = ExpectSkeletonWithEvent(kEventSerialized);
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    {
        ::testing::InSequence seam_order{};
        EXPECT_CALL(*skel_mocks.event, Allocate(::testing::_))
            .WillOnce([&sample_slot](score::mw::com::impl::SampleAllocateeGuard guard) {
                return score::testing::MakeAllocatedSample(&sample_slot, std::move(guard));
            });
        EXPECT_CALL(*skel_mocks.event, Send(::testing::_, ::testing::_))
            .WillOnce(
                [&first_received](
                    score::mw::com::impl::SampleAllocateePtr<void> sample,
                    std::optional<score::mw::com::impl::SkeletonEventBinding::SendTraceCallback>) {
                    auto const* typed =
                        static_cast<const serializer::PreSerializedData<kMaxMessageSize>*>(
                            sample.Get());
                    first_received.assign(typed->data, typed->data + typed->size);
                    return score::Result<void>{};
                });
        EXPECT_CALL(checkpoint, Call(std::string_view{"after_first_valid"})).Times(1);
        EXPECT_CALL(checkpoint, Call(std::string_view{"after_truncated_frames"})).Times(1);
        EXPECT_CALL(*skel_mocks.event, Allocate(::testing::_))
            .WillOnce([&sample_slot](score::mw::com::impl::SampleAllocateeGuard guard) {
                return score::testing::MakeAllocatedSample(&sample_slot, std::move(guard));
            });
        EXPECT_CALL(*skel_mocks.event, Send(::testing::_, ::testing::_))
            .WillOnce(
                [&second_received](
                    score::mw::com::impl::SampleAllocateePtr<void> sample,
                    std::optional<score::mw::com::impl::SkeletonEventBinding::SendTraceCallback>) {
                    auto const* typed =
                        static_cast<const serializer::PreSerializedData<kMaxMessageSize>*>(
                            sample.Get());
                    second_received.assign(typed->data, typed->data + typed->size);
                    return score::Result<void>{};
                });
    }

    auto skeleton_res = CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized});
    ASSERT_TRUE(skeleton_res.has_value());

    auto instance_res =
        RemoteServiceInstance::Create(default_cfg_.remote_instance, default_cfg_.service_type,
                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_TRUE(instance_res.has_value());

    callbacks.on_event_update(*cc_ptr, kEventIndex0,
                              MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, kFirstPayload));
    checkpoint.Call("after_first_valid");

    constexpr std::array<std::uint8_t, kTruncatedOneByteSize> kOneByte{kSomeipServiceIdMsb};
    constexpr std::array<std::uint8_t, kTruncatedFifteenByteSize> kFifteenBytes{
        0x12U, 0x34U, 0x80U, 0x01U, 0x00U, 0x00U, 0x00U, 0x08U,
        0xFFU, 0xFFU, 0x00U, 0x01U, 0x01U, 0x01U, 0x02U,
    };

    callbacks.on_event_update(*cc_ptr, kEventIndex0, socom::empty_payload());
    callbacks.on_event_update(*cc_ptr, kEventIndex0, MakePayloadFromBytes(kOneByte));
    callbacks.on_event_update(*cc_ptr, kEventIndex0, MakePayloadFromBytes(kFifteenBytes));
    checkpoint.Call("after_truncated_frames");

    callbacks.on_event_update(*cc_ptr, kEventIndex0,
                              MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, kSecondPayload));

    ASSERT_EQ(first_received.size(), kFourBytePayloadSize);
    EXPECT_THAT(first_received, ::testing::ElementsAreArray(kFirstExpected));
    ASSERT_EQ(second_received.size(), kFourBytePayloadSize);
    EXPECT_THAT(second_received, ::testing::ElementsAreArray(kSecondExpected));
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(RemoteServiceInstanceTest, DISABLED_CreateRejectsNullConfigurationFields) {
    auto const null_name_cfg = BuildConfigWithRawServiceType({false, true});
    auto const null_events_cfg = BuildConfigWithRawServiceType({true, false});

    struct Case {
        std::string_view label;
        std::shared_ptr<const mw_someip_config::ServiceInstance> instance_cfg;
        std::shared_ptr<const mw_someip_config::ServiceType> type_cfg;
    };

    std::array<Case, 4U> const cases{{
        {"null_service_instance_config", nullptr, default_cfg_.service_type},
        {"null_service_type_config", default_cfg_.remote_instance, nullptr},
        {"null_service_type_name", null_name_cfg.remote_instance, null_name_cfg.service_type},
        {"null_events_vector", null_events_cfg.remote_instance, null_events_cfg.service_type},
    }};

    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    EXPECT_CALL(service_discovery_mock(), OfferService(::testing::_)).Times(0);
    EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
        .Times(0);

    for (std::size_t i{0U}; i < cases.size(); ++i) {
        auto const skel_mocks = ExpectSkeletonWithEvent(kEventSerialized);
        EXPECT_CALL(*skel_mocks.skeleton, PrepareOffer(::testing::_, ::testing::_, ::testing::_))
            .Times(0);
    }

    std::vector<score::mw::com::GenericSkeleton> skeletons{};
    skeletons.reserve(cases.size());
    for (std::size_t i{0U}; i < cases.size(); ++i) {
        auto skeleton_res = CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized});
        ASSERT_TRUE(skeleton_res.has_value());
        skeletons.push_back(std::move(*skeleton_res));
    }

    for (std::size_t i{0U}; i < cases.size(); ++i) {
        auto const& tc = cases[i];
        SCOPED_TRACE(tc.label);
        auto const result = RemoteServiceInstance::Create(tc.instance_cfg, tc.type_cfg,
                                                          std::move(skeletons[i]), socom_runtime_);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), score::mw::com::ComErrc::kInvalidConfiguration);
    }
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(RemoteServiceInstanceTest, DISABLED_CreateAsyncRemoteServiceRejectsNullConfigurationFields) {
    auto const null_name_cfg = BuildConfigWithRawServiceType({false, true});
    auto const null_events_cfg = BuildConfigWithRawServiceType({true, false});

    struct Case {
        std::string_view label;
        std::shared_ptr<const mw_someip_config::ServiceInstance> instance_cfg;
        std::shared_ptr<const mw_someip_config::ServiceType> type_cfg;
    };

    std::array<Case, 4U> const cases{{
        {"null_service_instance_config", nullptr, default_cfg_.service_type},
        {"null_service_type_config", default_cfg_.remote_instance, nullptr},
        {"null_service_type_name", null_name_cfg.remote_instance, null_name_cfg.service_type},
        {"null_events_vector", null_events_cfg.remote_instance, null_events_cfg.service_type},
    }};

    EXPECT_CALL(service_discovery_mock(), OfferService(::testing::_)).Times(0);
    EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
        .Times(0);

    std::vector<std::unique_ptr<RemoteServiceInstance>> instances{};
    for (auto const& tc : cases) {
        SCOPED_TRACE(tc.label);
        auto const result = RemoteServiceInstance::CreateAsyncRemoteService(
            tc.instance_cfg, tc.type_cfg, socom_runtime_, instances);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), score::mw::com::ComErrc::kInvalidConfiguration);
        EXPECT_TRUE(instances.empty());
    }
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(RemoteServiceInstanceTest,
       DISABLED_SubscribesOnlyEventsPresentOnSkeletonAndDropsSkippedOrUnknownIds) {
    auto const cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
        EventSpec{kEventMissingInIpc, kEventMissingInIpcId, kMissingIpcEventMaxSize},
        EventSpec{kEventSerialized2, kEventSerialized2Id, kMaxMessageSize2},
    });
    ReinitSerializer(cfg);

    serializer::PreSerializedData<kMaxMessageSize2> event2_slot{};
    socom::Client_connector::Callbacks callbacks{};
    std::uint16_t captured_num_events{0U};

    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized, kEventSerialized2});
    auto const skel_mocks = ExpectSkeletonWithEvents({kEventSerialized, kEventSerialized2});
    ASSERT_EQ(skel_mocks.events.size(), kTwoSkeletonEvents);

    auto cc_mock = std::make_unique<::testing::NiceMock<socom::Client_connector_mock>>();
    auto* const cc_ptr = cc_mock.get();
    EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
        .InSequence(client_connector_sequence())
        .WillOnce([&callbacks, &captured_num_events, cc = std::move(cc_mock)](
                      socom::Service_interface_definition def, socom::Service_instance,
                      socom::Client_connector::Callbacks cbs) mutable
                      -> score::Result<socom::Client_connector::Uptr> {
            captured_num_events = def.num_events;
            callbacks = std::move(cbs);
            return std::move(cc);
        });

    EXPECT_CALL(*cc_ptr, subscribe_event(kEventIndex1, ::testing::_)).Times(0);
    EXPECT_CALL(*cc_ptr, subscribe_event(kEventIndex0, socom::Event_mode::update))
        .WillOnce(::testing::Return(score::Result<void>{}));
    EXPECT_CALL(*cc_ptr, subscribe_event(kEventIndex2, socom::Event_mode::update))
        .WillOnce(::testing::Return(score::Result<void>{}));

    EXPECT_CALL(*skel_mocks.events[0], Allocate(::testing::_)).Times(0);
    EXPECT_CALL(*skel_mocks.events[0], Send(::testing::_, ::testing::_)).Times(0);

    EXPECT_CALL(*skel_mocks.events[1], Allocate(::testing::_))
        .WillOnce([&event2_slot](score::mw::com::impl::SampleAllocateeGuard guard) {
            return score::testing::MakeAllocatedSample(&event2_slot, std::move(guard));
        });
    EXPECT_CALL(*skel_mocks.events[1], Send(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(score::Result<void>{}));

    auto skeleton_res =
        CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized, kEventSerialized2});
    ASSERT_TRUE(skeleton_res.has_value());

    auto instance_res = RemoteServiceInstance::Create(cfg.remote_instance, cfg.service_type,
                                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_TRUE(instance_res.has_value());
    EXPECT_EQ(captured_num_events, kThreeConfiguredEvents);

    auto const server_def = MakeServerDefinition(kThreeConfiguredEvents);
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::not_available, server_def);
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    constexpr std::array<std::uint8_t, kFourBytePayloadSize> kPayloadBytes{0x91U, 0x92U, 0x93U,
                                                                           0x94U};
    constexpr std::array<std::byte, kFourBytePayloadSize> kExpectedBytes{
        std::byte{0x91U}, std::byte{0x92U}, std::byte{0x93U}, std::byte{0x94U}};

    callbacks.on_event_update(*cc_ptr, kEventIndex1,
                              MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, kPayloadBytes));
    callbacks.on_event_update(*cc_ptr, kUnknownEventId,
                              MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, kPayloadBytes));
    callbacks.on_event_update(*cc_ptr, kEventIndex2,
                              MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, kPayloadBytes));

    ASSERT_EQ(event2_slot.size, kFourBytePayloadSize);
    score::cpp::span<const std::byte> const actual_payload{event2_slot.data, event2_slot.size};
    EXPECT_THAT(actual_payload, ::testing::ElementsAreArray(kExpectedBytes));
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(RemoteServiceInstanceTest,
       DISABLED_SubscribesOnlyEventsWithSerializerAndDropsSkippedEventUpdates) {
    auto const cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
        EventSpec{kEventNoSerializer, kEventNoSerializerId, std::nullopt},
        EventSpec{kEventSerialized2, kEventSerialized2Id, kMaxMessageSize2},
    });
    ReinitSerializer(cfg);

    serializer::PreSerializedData<kMaxMessageSize2> event2_slot{};
    socom::Client_connector::Callbacks callbacks{};

    StubInstanceResolved(kRemoteSpecifier,
                         {kEventSerialized, kEventNoSerializer, kEventSerialized2});
    auto const skel_mocks =
        ExpectSkeletonWithEvents({kEventSerialized, kEventNoSerializer, kEventSerialized2});
    ASSERT_EQ(skel_mocks.events.size(), kThreeConfiguredEvents);

    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    EXPECT_CALL(*cc_ptr, subscribe_event(kEventIndex1, ::testing::_)).Times(0);
    EXPECT_CALL(*cc_ptr, subscribe_event(kEventIndex0, socom::Event_mode::update))
        .WillOnce(::testing::Return(score::Result<void>{}));
    EXPECT_CALL(*cc_ptr, subscribe_event(kEventIndex2, socom::Event_mode::update))
        .WillOnce(::testing::Return(score::Result<void>{}));

    EXPECT_CALL(*skel_mocks.events[0], Allocate(::testing::_)).Times(0);
    EXPECT_CALL(*skel_mocks.events[0], Send(::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*skel_mocks.events[1], Allocate(::testing::_)).Times(0);
    EXPECT_CALL(*skel_mocks.events[1], Send(::testing::_, ::testing::_)).Times(0);

    EXPECT_CALL(*skel_mocks.events[2], Allocate(::testing::_))
        .WillOnce([&event2_slot](score::mw::com::impl::SampleAllocateeGuard guard) {
            return score::testing::MakeAllocatedSample(&event2_slot, std::move(guard));
        });
    EXPECT_CALL(*skel_mocks.events[2], Send(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(score::Result<void>{}));

    auto skeleton_res = CreateGenericSkeleton(
        kRemoteSpecifier, {kEventSerialized, kEventNoSerializer, kEventSerialized2});
    ASSERT_TRUE(skeleton_res.has_value());

    auto instance_res = RemoteServiceInstance::Create(cfg.remote_instance, cfg.service_type,
                                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_TRUE(instance_res.has_value());

    auto const server_def = MakeServerDefinition(kThreeConfiguredEvents);
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    constexpr std::array<std::uint8_t, kFourBytePayloadSize> kPayloadBytes{0xA1U, 0xA2U, 0xA3U,
                                                                           0xA4U};
    constexpr std::array<std::byte, kFourBytePayloadSize> kExpectedBytes{
        std::byte{0xA1U}, std::byte{0xA2U}, std::byte{0xA3U}, std::byte{0xA4U}};

    callbacks.on_event_update(*cc_ptr, kEventIndex1,
                              MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, kPayloadBytes));
    callbacks.on_event_update(*cc_ptr, kEventIndex2,
                              MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, kPayloadBytes));

    ASSERT_EQ(event2_slot.size, kFourBytePayloadSize);
    score::cpp::span<const std::byte> const actual_payload{event2_slot.data, event2_slot.size};
    EXPECT_THAT(actual_payload, ::testing::ElementsAreArray(kExpectedBytes));
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(RemoteServiceInstanceTest,
       DISABLED_DestructorStopsInFlightCallbacksBeforeEventContextsDestruction) {
    serializer::PreSerializedData<kMaxMessageSize> sample_slot{};
    constexpr std::array<std::uint8_t, kFourBytePayloadSize> kInFlightPayload{0x51U, 0x52U, 0x53U,
                                                                              0x54U};
    constexpr std::array<std::byte, kFourBytePayloadSize> kExpectedBytes{
        std::byte{0x51U}, std::byte{0x52U}, std::byte{0x53U}, std::byte{0x54U}};
    ::testing::MockFunction<void(std::string_view)> checkpoint{};

    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    auto const skel_mocks = ExpectSkeletonWithEvent(kEventSerialized);

    auto connector = std::make_unique<::testing::NiceMock<TrackableClientConnectorMock>>(
        [&kInFlightPayload](socom::Client_connector const& self,
                            socom::Client_connector::Callbacks& active_callbacks) {
            active_callbacks.on_event_update(
                self, kEventIndex0,
                MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, kInFlightPayload));
        });
    auto* const connector_raw = connector.get();
    bool connector_created{false};

    EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
        .InSequence(client_connector_sequence())
        .WillOnce([&connector_created, connector_raw, cc = std::move(connector)](
                      socom::Service_interface_definition, socom::Service_instance,
                      socom::Client_connector::Callbacks callbacks) mutable
                      -> score::Result<socom::Client_connector::Uptr> {
            connector_raw->SetCallbacks(std::move(callbacks));
            connector_created = true;
            return std::move(cc);
        });

    EXPECT_CALL(service_discovery_mock(), OfferService(::testing::_))
        .WillOnce(::testing::Return(score::Result<void>{}));

    {
        ::testing::InSequence teardown_order{};
        EXPECT_CALL(checkpoint, Call(std::string_view{"before_destroy"})).Times(1);
        EXPECT_CALL(*skel_mocks.event, Allocate(::testing::_))
            .WillOnce([&sample_slot](score::mw::com::impl::SampleAllocateeGuard guard) {
                return score::testing::MakeAllocatedSample(&sample_slot, std::move(guard));
            });
        EXPECT_CALL(*skel_mocks.event, Send(::testing::_, ::testing::_))
            .WillOnce(::testing::Return(score::Result<void>{}));
        EXPECT_CALL(*connector_raw, Die()).Times(1);
        EXPECT_CALL(*skel_mocks.skeleton, PrepareStopOffer(::testing::_)).Times(1);
    }

    auto skeleton_res = CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized});
    ASSERT_TRUE(skeleton_res.has_value());

    auto instance_res =
        RemoteServiceInstance::Create(default_cfg_.remote_instance, default_cfg_.service_type,
                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_TRUE(instance_res.has_value());
    ASSERT_TRUE(connector_created);

    checkpoint.Call("before_destroy");
    instance_res->reset();

    ASSERT_EQ(sample_slot.size, kFourBytePayloadSize);
    score::cpp::span<const std::byte> const actual_payload{sample_slot.data, sample_slot.size};
    EXPECT_THAT(actual_payload, ::testing::ElementsAreArray(kExpectedBytes));
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(RemoteServiceInstanceTest,
       DISABLED_AvailableCallbackContinuesSubscribingRemainingEventsWhenOneSubscribeFails) {
    struct SkeletonEventCapture {
        std::size_t allocate_calls{0U};
        std::size_t send_calls{0U};
        void const* sent_ptr{nullptr};
    };

    constexpr int kAtMostOneCall{1};
    constexpr int kExpectedSubscribeCalls{2};
    constexpr std::size_t kFirstSubscribeCall{1U};
    constexpr std::size_t kSecondSubscribeCall{2U};

    auto const cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
        EventSpec{kEventSerialized2, kEventSerialized2Id, kMaxMessageSize2},
    });
    ASSERT_NO_FATAL_FAILURE(ReinitSerializer(cfg));

    serializer::PreSerializedData<kMaxMessageSize> event0_slot{};
    serializer::PreSerializedData<kMaxMessageSize2> event1_slot{};
    SkeletonEventCapture event0_capture{};
    SkeletonEventCapture event1_capture{};
    socom::Client_connector::Callbacks callbacks{};

    std::size_t subscribe_call_count{0U};
    std::optional<socom::Event_id> failed_event_id{};
    std::optional<socom::Event_id> succeeded_event_id{};

    StubInstanceResolved(kRemoteSpecifier, {kEventSerialized, kEventSerialized2});
    auto const skel_mocks = ExpectSkeletonWithEvents({kEventSerialized, kEventSerialized2});
    ASSERT_EQ(skel_mocks.events.size(), kTwoSkeletonEvents);
    auto* const ev0_mock = skel_mocks.events[0];
    auto* const ev1_mock = skel_mocks.events[1];
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    EXPECT_CALL(*ev0_mock, Allocate(::testing::_))
        .Times(::testing::AtMost(kAtMostOneCall))
        .WillRepeatedly(
            [&event0_slot, &event0_capture](score::mw::com::impl::SampleAllocateeGuard guard) {
                ++event0_capture.allocate_calls;
                return score::testing::MakeAllocatedSample(&event0_slot, std::move(guard));
            });
    EXPECT_CALL(*ev0_mock, Send(::testing::_, ::testing::_))
        .Times(::testing::AtMost(kAtMostOneCall))
        .WillRepeatedly(
            [&event0_capture](
                score::mw::com::impl::SampleAllocateePtr<void> sample,
                std::optional<score::mw::com::impl::SkeletonEventBinding::SendTraceCallback>) {
                ++event0_capture.send_calls;
                event0_capture.sent_ptr = sample.Get();
                return score::Result<void>{};
            });

    EXPECT_CALL(*ev1_mock, Allocate(::testing::_))
        .Times(::testing::AtMost(kAtMostOneCall))
        .WillRepeatedly(
            [&event1_slot, &event1_capture](score::mw::com::impl::SampleAllocateeGuard guard) {
                ++event1_capture.allocate_calls;
                return score::testing::MakeAllocatedSample(&event1_slot, std::move(guard));
            });
    EXPECT_CALL(*ev1_mock, Send(::testing::_, ::testing::_))
        .Times(::testing::AtMost(kAtMostOneCall))
        .WillRepeatedly(
            [&event1_capture](
                score::mw::com::impl::SampleAllocateePtr<void> sample,
                std::optional<score::mw::com::impl::SkeletonEventBinding::SendTraceCallback>) {
                ++event1_capture.send_calls;
                event1_capture.sent_ptr = sample.Get();
                return score::Result<void>{};
            });

    EXPECT_CALL(*cc_ptr, subscribe_event(::testing::_, socom::Event_mode::update))
        .Times(kExpectedSubscribeCalls)
        .WillRepeatedly(
            [&subscribe_call_count, &failed_event_id, &succeeded_event_id](
                socom::Event_id const event_id, socom::Event_mode) -> score::Result<void> {
                ++subscribe_call_count;
                if (subscribe_call_count == kFirstSubscribeCall) {
                    failed_event_id = event_id;
                    return score::MakeUnexpected(
                        socom::Server_connector_error::logic_error_id_out_of_range);
                }
                if (subscribe_call_count == kSecondSubscribeCall) {
                    succeeded_event_id = event_id;
                }
                return score::Result<void>{};
            });

    EXPECT_CALL(*skel_mocks.skeleton, PrepareStopOffer(::testing::_)).Times(1);

    auto skeleton_res =
        CreateGenericSkeleton(kRemoteSpecifier, {kEventSerialized, kEventSerialized2});
    ASSERT_TRUE(skeleton_res.has_value());

    auto instance_res = RemoteServiceInstance::Create(cfg.remote_instance, cfg.service_type,
                                                      std::move(*skeleton_res), socom_runtime_);
    ASSERT_TRUE(instance_res.has_value());

    auto const server_def = MakeServerDefinition(kTwoSkeletonEvents);
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    EXPECT_EQ(subscribe_call_count, kTwoSkeletonEvents);
    ASSERT_TRUE(failed_event_id.has_value());
    ASSERT_TRUE(succeeded_event_id.has_value());
    EXPECT_NE(*failed_event_id, *succeeded_event_id);
    EXPECT_THAT(*failed_event_id, ::testing::AnyOf(kEventIndex0, kEventIndex1));
    EXPECT_THAT(*succeeded_event_id, ::testing::AnyOf(kEventIndex0, kEventIndex1));

    constexpr std::array<std::uint8_t, kFourBytePayloadSize> kPayloadBytes{0x00U, 0x7FU, 0x80U,
                                                                           0xFFU};
    constexpr std::array<std::byte, kFourBytePayloadSize> kExpectedBytes{
        std::byte{0x00U}, std::byte{0x7FU}, std::byte{0x80U}, std::byte{0xFFU}};
    callbacks.on_event_update(*cc_ptr, *succeeded_event_id,
                              MakeFramedSomeipPayload(kSomeipFourByteLengthLsb, kPayloadBytes));

    auto const is_event0_succeeded = (*succeeded_event_id == kEventIndex0);
    auto const& succeeded_capture = is_event0_succeeded ? event0_capture : event1_capture;
    auto const& failed_capture = is_event0_succeeded ? event1_capture : event0_capture;
    void const* const expected_slot_ptr = is_event0_succeeded
                                              ? static_cast<void const*>(&event0_slot)
                                              : static_cast<void const*>(&event1_slot);
    std::size_t const actual_size = is_event0_succeeded ? event0_slot.size : event1_slot.size;
    std::byte const* const actual_data = is_event0_succeeded ? event0_slot.data : event1_slot.data;

    EXPECT_EQ(succeeded_capture.allocate_calls, kSingleElementCount);
    EXPECT_EQ(succeeded_capture.send_calls, kSingleElementCount);
    EXPECT_EQ(succeeded_capture.sent_ptr, expected_slot_ptr);
    ASSERT_EQ(actual_size, kFourBytePayloadSize);
    score::cpp::span<const std::byte> const actual_payload{actual_data, actual_size};
    EXPECT_THAT(actual_payload, ::testing::ElementsAreArray(kExpectedBytes));

    EXPECT_EQ(failed_capture.allocate_calls, kZeroBytes);
    EXPECT_EQ(failed_capture.send_calls, kZeroBytes);
    EXPECT_EQ(failed_capture.sent_ptr, nullptr);

    instance_res->reset();
}

}  // namespace
}  // namespace score::someip_gateway::gatewayd
