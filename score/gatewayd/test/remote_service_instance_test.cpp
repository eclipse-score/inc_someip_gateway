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
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <score/span.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "score/containers/non_relocatable_vector.h"
#include "score/gateway_ipc_binding/test/unit/mw_com_mocks.hpp"
#include "score/gatewayd/test/service_instance_test_fixture.h"
#include "score/mw/com/com_error_domain.h"
#include "score/mw/com/impl/com_error.h"
#include "score/mw/com/impl/plumbing/sample_allocatee_ptr.h"
#include "score/mw/com/types.h"
#include "score/result/result.h"
#include "score/serializer/pre_serialized_data.h"
#include "score/socom/client_connector.hpp"
#include "score/socom/client_connector_mock.hpp"
#include "score/socom/error.hpp"
#include "score/socom/payload.hpp"
#include "score/socom/runtime_mock.hpp"
#include "score/socom/service_interface_definition.hpp"
#include "score/someip/constants.h"

namespace score::someip_gateway::gatewayd {
namespace {

namespace ipc_testing = score::gateway_ipc_binding::testing;

using ::testing::_;
using ::testing::ByMove;
using ::testing::NiceMock;
using ::testing::Return;

using score::someip_gateway::serializer::PreSerializedData;

constexpr std::size_t kWirePayloadBodySize{4U};
constexpr std::size_t kWirePacketSize{someip::kSomeipFullHeaderSize + kWirePayloadBodySize};
constexpr std::size_t kOversizedWirePacketSize{someip::kSomeipFullHeaderSize + kMaxMessageSize +
                                               1U};
constexpr std::array<std::byte, kWirePayloadBodySize> kWirePayloadBytes{
    std::byte{0x11U}, std::byte{0x22U}, std::byte{0x33U}, std::byte{0x44U}};
constexpr std::byte kCorruptFillByte{0xAAU};
constexpr std::uint8_t kCorruptEventNameChar{static_cast<std::uint8_t>('X')};

constexpr socom::Event_id kSerializedSocomId{0U};
constexpr socom::Event_id kSecondSocomId{1U};
constexpr socom::Event_id kThirdSocomId{2U};
constexpr std::uint16_t kZeroMethodsCount{0U};
constexpr std::size_t kSingleInstanceCount{1U};

std::array<std::byte, kWirePacketSize> MakeValidWireBytes() noexcept {
    std::array<std::byte, kWirePacketSize> wire{};
    (void)std::copy(kWirePayloadBytes.cbegin(), kWirePayloadBytes.cend(),
                    wire.begin() + someip::kSomeipFullHeaderSize);
    return wire;
}

socom::Payload MakeSpanPayload(score::cpp::span<std::byte const> const bytes) {
    return socom::Payload{bytes, socom::kNoSlotHandle, []() noexcept {}};
}

class RemoteServiceInstanceTest : public ServiceInstanceTestFixture {
   protected:
    RemoteServiceInstanceTest() { StubInstanceResolved(kRemoteSpecifier, {kEventSerialized}); }

    struct MockIpcSkeleton {
        score::mw::com::GenericSkeleton skeleton;
        ipc_testing::Skeleton* skeleton_binding{nullptr};
        ipc_testing::GenericSkeletonEvent* serialized_event{nullptr};
    };

    struct ConnectedRemoteInstance {
        std::unique_ptr<RemoteServiceInstance> instance{};
        socom::Client_connector::Callbacks callbacks{};
        NiceMock<socom::Client_connector_mock>* client_connector{nullptr};
        ipc_testing::GenericSkeletonEvent* serialized_event{nullptr};
    };

    MockIpcSkeleton CreateMockIpcSkeleton(
        std::initializer_list<std::string_view> const event_names = {kEventSerialized}) {
        StubInstanceResolved(kRemoteSpecifier, event_names);

        auto skel_mock = std::make_unique<NiceMock<ipc_testing::Skeleton>>();
        auto* const skel_mock_ptr = skel_mock.get();
        EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
            .WillOnce(Return(ByMove(std::move(skel_mock))));

        ipc_testing::GenericSkeletonEvent* serialized_event_ptr{nullptr};
        score::containers::NonRelocatableVector<score::mw::com::EventInfo> events(
            event_names.size());
        score::mw::com::DataTypeMetaInfo const meta{sizeof(PreSerializedData<kMaxMessageSize>),
                                                    alignof(PreSerializedData<kMaxMessageSize>)};

        for (auto const name : event_names) {
            auto event_mock = std::make_unique<NiceMock<ipc_testing::GenericSkeletonEvent>>();
            auto* const event_ptr = event_mock.get();
            if (name == kEventSerialized) {
                serialized_event_ptr = event_ptr;
            }
            EXPECT_CALL(generic_skeleton_event_factory_mock(), Create(_, _, name, _))
                .WillOnce(Return(ByMove(std::move(event_mock))));
            events.emplace_back(score::mw::com::EventInfo{name, meta});
        }

        auto const spec =
            score::mw::com::InstanceSpecifier::Create(std::string{kRemoteSpecifier}).value();
        score::mw::com::GenericSkeletonServiceElementInfo info{};
        info.events = std::move(events);
        return MockIpcSkeleton{score::mw::com::GenericSkeleton::Create(spec, info).value(),
                               skel_mock_ptr, serialized_event_ptr};
    }

    ConnectedRemoteInstance CreateConnectedInstance(
        std::initializer_list<std::string_view> const ipc_events = {kEventSerialized}) {
        return CreateConnectedInstance(default_cfg_, ipc_events);
    }

    ConnectedRemoteInstance CreateConnectedInstance(
        BuiltConfig const& cfg, std::initializer_list<std::string_view> const ipc_events = {
                                    kEventNoSerializer, kEventSerialized}) {
        auto mock_skel = CreateMockIpcSkeleton(ipc_events);
        ConnectedRemoteInstance connected{};
        connected.serialized_event = mock_skel.serialized_event;
        connected.client_connector = ExpectClientConnectorCreated(&connected.callbacks);

        auto inst_res = RemoteServiceInstance::Create(
            cfg.remote_instance, cfg.service_type, std::move(mock_skel.skeleton), socom_runtime_);
        EXPECT_TRUE(inst_res.has_value());
        connected.instance = std::move(inst_res).value();
        return connected;
    }

    std::vector<std::unique_ptr<RemoteServiceInstance>> instances_{};
};

// Returns ComErrc::kInvalidConfiguration when service_instance_config is null.
TEST_F(RemoteServiceInstanceTest, CreateAsyncRemoteServiceFailsWhenInstanceConfigIsNull) {
    auto const res = RemoteServiceInstance::CreateAsyncRemoteService(
        nullptr, default_cfg_.service_type, socom_runtime_, instances_);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), score::mw::com::ComErrc::kInvalidConfiguration);
}

// Returns ComErrc::kInvalidConfiguration when any configured event has no registered serializer.
TEST_F(RemoteServiceInstanceTest, CreateAsyncRemoteServiceFailsWhenEventLacksSerializer) {
    auto const bad_ser_cfg = BuildConfig({
        EventSpec{kEventNoSerializer, kEventNoSerializerId, std::nullopt},
    });

    auto const res = RemoteServiceInstance::CreateAsyncRemoteService(
        bad_ser_cfg.remote_instance, bad_ser_cfg.service_type, socom_runtime_, instances_);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), score::mw::com::ComErrc::kInvalidConfiguration);
}

// Returns ComErrc::kInvalidConfiguration when GenericSkeleton::Create() fails for an unresolved
// InstanceSpecifier.
TEST_F(RemoteServiceInstanceTest, CreateAsyncRemoteServiceFailsWhenIpcSkeletonCreationFails) {
    StubInstanceNotResolved();

    auto const res = RemoteServiceInstance::CreateAsyncRemoteService(
        default_cfg_.remote_instance, default_cfg_.service_type, socom_runtime_, instances_);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), score::mw::com::ComErrc::kInvalidConfiguration);
}

// Returns ComErrc::kBindingFailure without creating a SOCom Client_connector when
// GenericSkeleton::OfferService() fails.
TEST_F(RemoteServiceInstanceTest, CreateAsyncRemoteServiceFailsWhenSkeletonOfferFails) {
    auto const mocks = ExpectSkeletonWithEvent(kEventSerialized);
    EXPECT_CALL(*mocks.skeleton, PrepareOffer(_, _, _))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));

    auto const res = RemoteServiceInstance::CreateAsyncRemoteService(
        default_cfg_.remote_instance, default_cfg_.service_type, socom_runtime_, instances_);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), score::mw::com::ComErrc::kBindingFailure);
}

// Appends the created RemoteServiceInstance to instances so its GenericSkeleton and SOCom
// Client_connector are not destroyed when CreateAsyncRemoteService() returns.
TEST_F(RemoteServiceInstanceTest, CreateAsyncRemoteServiceAppendsCreatedInstanceOnSuccess) {
    (void)ExpectSkeletonWithEvent(kEventSerialized);
    (void)ExpectClientConnectorCreated();

    auto const res = RemoteServiceInstance::CreateAsyncRemoteService(
        default_cfg_.remote_instance, default_cfg_.service_type, socom_runtime_, instances_);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(instances_.size(), kSingleInstanceCount);
}

// Returns runtime_error_request_rejected when SOCom make_client_connector() fails.
TEST_F(RemoteServiceInstanceTest, CreateFailsWhenMakeClientConnectorFails) {
    auto const cfg = BuildConfig({
        EventSpec{kEventMissingInIpc, kEventMissingInIpcId, kMaxMessageSize},
        EventSpec{kEventNoSerializer, kEventNoSerializerId, std::nullopt},
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    });
    auto mock_skel = CreateMockIpcSkeleton({kEventNoSerializer, kEventSerialized});

    EXPECT_CALL(socom_runtime_, make_client_connector(_, _, _))
        .WillOnce(Return(ByMove(score::MakeUnexpected<socom::Client_connector::Uptr>(
            socom::Construction_error::callback_missing))));

    auto const res = RemoteServiceInstance::Create(cfg.remote_instance, cfg.service_type,
                                                   std::move(mock_skel.skeleton), socom_runtime_);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), socom::Error::runtime_error_request_rejected);
}

// Does not call subscribe_event() while the SOCom service is not_available, and subscribes every
// configured Event_id in Event_mode::update when it becomes available.
TEST_F(RemoteServiceInstanceTest, ServiceStateTransitionSubscribesEventsOnlyWhenAvailable) {
    auto const cfg = BuildConfig({
        EventSpec{kEventMissingInIpc, kEventMissingInIpcId, kMaxMessageSize},
        EventSpec{kEventNoSerializer, kEventNoSerializerId, std::nullopt},
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    });
    auto const connected = CreateConnectedInstance(cfg);
    socom::Server_service_interface_definition const dummy_def{
        socom::Service_interface_identifier{kServiceTypeName,
                                            {kServiceMajorVersion, kServiceMinorVersion}},
        socom::to_num_of_methods(kZeroMethodsCount),
        socom::to_num_of_events(cfg.service_type->events()->size())};

    EXPECT_CALL(*connected.client_connector, subscribe_event(_, _)).Times(0);
    connected.callbacks.on_service_state_change(*connected.client_connector,
                                                socom::Service_state::not_available, dummy_def);

    EXPECT_CALL(*connected.client_connector,
                subscribe_event(kSerializedSocomId, socom::Event_mode::update))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*connected.client_connector,
                subscribe_event(kSecondSocomId, socom::Event_mode::update))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*connected.client_connector,
                subscribe_event(kThirdSocomId, socom::Event_mode::update))
        .WillOnce(Return(score::Result<void>{}));
    connected.callbacks.on_service_state_change(*connected.client_connector,
                                                socom::Service_state::available, dummy_def);
    connected.callbacks.on_event_requested_update(*connected.client_connector, kSerializedSocomId,
                                                  socom::empty_payload());
}

// Skips deserialization and Send() when LoLa Allocate() fails because no sample slot is free.
TEST_F(RemoteServiceInstanceTest, EventUpdateSkipsWhenSampleAllocationFails) {
    auto const connected = CreateConnectedInstance();
    auto const valid_wire = MakeValidWireBytes();

    EXPECT_CALL(*connected.serialized_event, Allocate(_))
        .WillOnce(
            Return(ByMove(score::MakeUnexpected<score::mw::com::impl::SampleAllocateePtr<void>>(
                score::mw::com::impl::ComErrc::kSampleAllocationFailure))));
    EXPECT_CALL(*connected.serialized_event, Send(_, _)).Times(0);

    connected.callbacks.on_event_update(
        *connected.client_connector, kSerializedSocomId,
        MakeSpanPayload(score::cpp::span<std::byte const>{valid_wire.data(), valid_wire.size()}));
}

// Skips Send() when the deserializer rejects an invalid or oversized SOME/IP payload.
TEST_F(RemoteServiceInstanceTest, EventUpdateSkipsWhenPayloadDeserializationFails) {
    auto const connected = CreateConnectedInstance();
    PreSerializedData<kMaxMessageSize> sample_storage{};

    EXPECT_CALL(*connected.serialized_event, Allocate(_))
        .WillOnce([&sample_storage](score::mw::com::impl::SampleAllocateeGuard guard) {
            return ipc_testing::MakeAllocatedSample(&sample_storage, std::move(guard));
        });
    EXPECT_CALL(*connected.serialized_event, Send(_, _)).Times(0);

    std::vector<std::byte> const oversized_wire(kOversizedWirePacketSize, kCorruptFillByte);
    connected.callbacks.on_event_update(*connected.client_connector, kSerializedSocomId,
                                        MakeSpanPayload(score::cpp::span<std::byte const>{
                                            oversized_wire.data(), oversized_wire.size()}));
}

// Logs and returns without aborting when GenericSkeletonEvent::Send() returns an error.
TEST_F(RemoteServiceInstanceTest, EventUpdateHandlesSkeletonSendFailureGracefully) {
    auto const connected = CreateConnectedInstance();
    PreSerializedData<kMaxMessageSize> sample_storage{};
    auto const valid_wire = MakeValidWireBytes();

    EXPECT_CALL(*connected.serialized_event, Allocate(_))
        .WillOnce([&sample_storage](score::mw::com::impl::SampleAllocateeGuard guard) {
            return ipc_testing::MakeAllocatedSample(&sample_storage, std::move(guard));
        });
    EXPECT_CALL(*connected.serialized_event, Send(_, _))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));

    connected.callbacks.on_event_update(
        *connected.client_connector, kSerializedSocomId,
        MakeSpanPayload(score::cpp::span<std::byte const>{valid_wire.data(), valid_wire.size()}));
}

// Skips the 16-byte SOME/IP header in the SOCom payload so only the event payload body is written
// into the LoLa sample.
TEST_F(RemoteServiceInstanceTest, EventUpdateDeserializesSomeipPacketAndSendsSample) {
    auto const connected = CreateConnectedInstance();
    PreSerializedData<kMaxMessageSize> sample_storage{};
    auto const valid_wire = MakeValidWireBytes();

    EXPECT_CALL(*connected.serialized_event, Allocate(_))
        .WillOnce([&sample_storage](score::mw::com::impl::SampleAllocateeGuard guard) {
            return ipc_testing::MakeAllocatedSample(&sample_storage, std::move(guard));
        });
    EXPECT_CALL(*connected.serialized_event, Send(_, _)).WillOnce(Return(score::Result<void>{}));

    connected.callbacks.on_event_update(
        *connected.client_connector, kSerializedSocomId,
        MakeSpanPayload(score::cpp::span<std::byte const>{valid_wire.data(), valid_wire.size()}));

    EXPECT_EQ(sample_storage.size, kWirePayloadBodySize);
    EXPECT_TRUE(std::equal(kWirePayloadBytes.cbegin(), kWirePayloadBytes.cend(),
                           std::cbegin(sample_storage.data)));
}

// Skips Allocate() and Send() when the incoming event's name is absent from
// GenericSkeleton::GetEvents().
TEST_F(RemoteServiceInstanceTest, EventUpdateDropsEventWhenMissingFromIpcSkeleton) {
    auto const connected = CreateConnectedInstance();
    auto const valid_wire = MakeValidWireBytes();

    auto const it = std::search(default_cfg_.buffer->begin(), default_cfg_.buffer->end(),
                                kEventSerialized.begin(), kEventSerialized.end());
    ASSERT_NE(it, default_cfg_.buffer->end());
    *it = kCorruptEventNameChar;

    EXPECT_CALL(*connected.serialized_event, Allocate(_)).Times(0);
    connected.callbacks.on_event_update(
        *connected.client_connector, kSerializedSocomId,
        MakeSpanPayload(score::cpp::span<std::byte const>{valid_wire.data(), valid_wire.size()}));
}

}  // namespace
}  // namespace score::someip_gateway::gatewayd
