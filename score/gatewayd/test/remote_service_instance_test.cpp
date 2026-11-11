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
#include "score/gatewayd/test/service_instance_test_fixture.h"
#include "score/mw/com/com_error_domain.h"
#include "score/mw/com/impl/bindings/mock_binding/sample_allocatee_ptr.h"
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

using ::testing::_;
using ::testing::ByMove;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

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

class RemoteServiceInstanceTest : public ServiceInstanceTestFixture {
   protected:
    struct MockIpcSkeleton {
        score::mw::com::GenericSkeleton skeleton;
        score::mw::com::impl::mock_binding::Skeleton* skeleton_binding{nullptr};
        score::mw::com::impl::mock_binding::GenericSkeletonEvent* serialized_event{nullptr};
    };

    struct ConnectedRemoteInstance {
        std::unique_ptr<RemoteServiceInstance> instance{};
        socom::Client_connector::Callbacks callbacks{};
        NiceMock<socom::Client_connector_mock>* client_connector{nullptr};
        score::mw::com::impl::mock_binding::GenericSkeletonEvent* serialized_event{nullptr};
    };

    static std::array<std::byte, kWirePacketSize> MakeValidWireBytes() noexcept {
        std::array<std::byte, kWirePacketSize> wire{};
        (void)std::copy(kWirePayloadBytes.cbegin(), kWirePayloadBytes.cend(),
                        wire.begin() + someip::kSomeipFullHeaderSize);
        return wire;
    }

    static socom::Payload MakeSpanPayload(score::cpp::span<std::byte const> const bytes) {
        return socom::Payload{bytes, socom::kNoSlotHandle, []() noexcept {}};
    }

    static score::Result<score::mw::com::impl::SampleAllocateePtr<void>> MakeAllocateeSample(
        void* const buffer, score::mw::com::impl::SampleAllocateeGuard guard) noexcept {
        score::mw::com::impl::mock_binding::SampleAllocateePtr mock_ptr{buffer,
                                                                        +[](void*) noexcept {}};
        return score::mw::com::impl::MakeSampleAllocateePtr(std::move(mock_ptr), std::move(guard));
    }

    void ExpectSkeletonFactoryOfferSuccess() {
        auto skel_mock = std::make_unique<NiceMock<score::mw::com::impl::mock_binding::Skeleton>>();
        auto* const skel_mock_ptr = skel_mock.get();
        auto skel_event_mock =
            std::make_unique<NiceMock<score::mw::com::impl::mock_binding::GenericSkeletonEvent>>();
        auto* const skel_event_ptr = skel_event_mock.get();

        EXPECT_CALL(*skel_mock_ptr, PrepareOffer(_, _, _)).WillOnce(Return(score::Result<void>{}));
        EXPECT_CALL(*skel_event_ptr, PrepareOffer(_)).WillOnce(Return(score::Result<void>{}));
        EXPECT_CALL(*skel_mock_ptr, VerifyAllMethodHandlersRegistered()).WillOnce(Return(true));
        EXPECT_CALL(service_discovery_mock(), OfferService(_))
            .WillOnce(Return(score::Result<void>{}));
        EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
            .WillOnce(Return(ByMove(std::move(skel_mock))));
        EXPECT_CALL(generic_skeleton_event_factory_mock(), Create(_, _, kEventSerialized, _))
            .WillOnce(Return(ByMove(std::move(skel_event_mock))));
    }

    MockIpcSkeleton CreateMockIpcSkeleton(
        std::initializer_list<std::string_view> const event_names = {kEventSerialized},
        bool const expect_offer_success = true) {
        ExpectInstanceResolved(kRemoteSpecifier, event_names);

        auto skel_mock = std::make_unique<NiceMock<score::mw::com::impl::mock_binding::Skeleton>>();
        auto* const skel_mock_ptr = skel_mock.get();
        EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
            .WillOnce(Return(ByMove(std::move(skel_mock))));

        if (expect_offer_success) {
            EXPECT_CALL(*skel_mock_ptr, PrepareOffer(_, _, _))
                .WillOnce(Return(score::Result<void>{}));
            EXPECT_CALL(*skel_mock_ptr, VerifyAllMethodHandlersRegistered()).WillOnce(Return(true));
            EXPECT_CALL(service_discovery_mock(), OfferService(_))
                .WillOnce(Return(score::Result<void>{}));
        }

        score::mw::com::impl::mock_binding::GenericSkeletonEvent* serialized_event_ptr{nullptr};
        score::containers::NonRelocatableVector<score::mw::com::EventInfo> events(
            event_names.size());
        score::mw::com::DataTypeMetaInfo const meta{sizeof(PreSerializedData<kMaxMessageSize>),
                                                    alignof(PreSerializedData<kMaxMessageSize>)};

        for (auto const name : event_names) {
            auto event_mock = std::make_unique<
                NiceMock<score::mw::com::impl::mock_binding::GenericSkeletonEvent>>();
            auto* const event_ptr = event_mock.get();
            if (name == kEventSerialized) {
                serialized_event_ptr = event_ptr;
            }
            if (expect_offer_success) {
                EXPECT_CALL(*event_ptr, PrepareOffer(_)).WillOnce(Return(score::Result<void>{}));
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
        BuiltConfig const& cfg, std::initializer_list<std::string_view> const ipc_events = {
                                    kEventNoSerializer, kEventSerialized}) {
        auto mock_skel = CreateMockIpcSkeleton(ipc_events);
        ConnectedRemoteInstance connected{};
        connected.serialized_event = mock_skel.serialized_event;

        EXPECT_CALL(socom_runtime_, make_client_connector(_, _, _))
            .WillOnce([&connected](socom::Service_interface_definition, socom::Service_instance,
                                   socom::Client_connector::Callbacks callbacks)
                          -> score::Result<socom::Client_connector::Uptr> {
                connected.callbacks = std::move(callbacks);
                auto mock = std::make_unique<NiceMock<socom::Client_connector_mock>>();
                connected.client_connector = mock.get();
                return mock;
            });

        auto inst_res = RemoteServiceInstance::Create(
            cfg.remote_instance, cfg.service_type, std::move(mock_skel.skeleton), socom_runtime_);
        EXPECT_TRUE(inst_res.has_value());
        connected.instance = std::move(inst_res).value();
        return connected;
    }

    StrictMock<socom::Runtime_mock> socom_runtime_{};
};

// Catches dereferencing a null service_instance_config when reading instance_specifier() instead of
// returning ComErrc::kInvalidConfiguration.
TEST_F(RemoteServiceInstanceTest, CreateAsyncRemoteServiceFailsWhenInstanceConfigIsNull) {
    std::vector<std::unique_ptr<RemoteServiceInstance>> instances{};
    auto const valid_cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    });

    auto const res = RemoteServiceInstance::CreateAsyncRemoteService(
        nullptr, valid_cfg.service_type, socom_runtime_, instances);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), score::mw::com::ComErrc::kInvalidConfiguration);
}

// Catches querying sizeof/alignof on a null serializer or skipping an event with no registered
// serializer instead of returning ComErrc::kInvalidConfiguration.
TEST_F(RemoteServiceInstanceTest, CreateAsyncRemoteServiceFailsWhenEventLacksSerializer) {
    std::vector<std::unique_ptr<RemoteServiceInstance>> instances{};
    auto const bad_ser_cfg = BuildConfig({
        EventSpec{kEventNoSerializer, kEventNoSerializerId, std::nullopt},
    });

    auto const res = RemoteServiceInstance::CreateAsyncRemoteService(
        bad_ser_cfg.remote_instance, bad_ser_cfg.service_type, socom_runtime_, instances);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), score::mw::com::ComErrc::kInvalidConfiguration);
}

// Catches calling .value() on a failed GenericSkeleton::Create result when the InstanceSpecifier
// cannot be resolved, instead of returning ComErrc::kInvalidConfiguration.
TEST_F(RemoteServiceInstanceTest, CreateAsyncRemoteServiceFailsWhenIpcSkeletonCreationFails) {
    std::vector<std::unique_ptr<RemoteServiceInstance>> instances{};
    auto const valid_cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    });
    ExpectInstanceNotResolved();

    auto const res = RemoteServiceInstance::CreateAsyncRemoteService(
        valid_cfg.remote_instance, valid_cfg.service_type, socom_runtime_, instances);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), score::mw::com::ComErrc::kInvalidConfiguration);
}

// Catches ignoring an ipc_skeleton.OfferService() failure and still creating a SOCom
// Client_connector for an unoffered LoLa skeleton instead of returning ComErrc::kBindingFailure.
TEST_F(RemoteServiceInstanceTest, CreateAsyncRemoteServiceFailsWhenSkeletonOfferFails) {
    std::vector<std::unique_ptr<RemoteServiceInstance>> instances{};
    auto const valid_cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    });
    ExpectInstanceResolved(kRemoteSpecifier, {kEventSerialized});

    auto skel_mock = std::make_unique<NiceMock<score::mw::com::impl::mock_binding::Skeleton>>();
    auto* const skel_mock_ptr = skel_mock.get();
    auto skel_event_mock =
        std::make_unique<NiceMock<score::mw::com::impl::mock_binding::GenericSkeletonEvent>>();

    EXPECT_CALL(*skel_mock_ptr, PrepareOffer(_, _, _))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
        .WillOnce(Return(ByMove(std::move(skel_mock))));
    EXPECT_CALL(generic_skeleton_event_factory_mock(), Create(_, _, kEventSerialized, _))
        .WillOnce(Return(ByMove(std::move(skel_event_mock))));

    auto const res = RemoteServiceInstance::CreateAsyncRemoteService(
        valid_cfg.remote_instance, valid_cfg.service_type, socom_runtime_, instances);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), score::mw::com::ComErrc::kBindingFailure);
}

// CreateAsyncRemoteService returns Result<void>, so forgetting to push the new instance into
// instances would destroy it on return and tear down its offered skeleton and Client_connector.
TEST_F(RemoteServiceInstanceTest, CreateAsyncRemoteServiceAppendsCreatedInstanceOnSuccess) {
    std::vector<std::unique_ptr<RemoteServiceInstance>> instances{};
    auto const valid_cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    });
    ExpectInstanceResolved(kRemoteSpecifier, {kEventSerialized});
    ExpectSkeletonFactoryOfferSuccess();

    EXPECT_CALL(socom_runtime_, make_client_connector(_, _, _))
        .WillOnce(
            [](socom::Service_interface_definition, socom::Service_instance,
               socom::Client_connector::Callbacks) -> score::Result<socom::Client_connector::Uptr> {
                return std::make_unique<NiceMock<socom::Client_connector_mock>>();
            });

    auto const res = RemoteServiceInstance::CreateAsyncRemoteService(
        valid_cfg.remote_instance, valid_cfg.service_type, socom_runtime_, instances);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(instances.size(), kSingleInstanceCount);
}

// Catches aborting on connector_result.value() or returning an instance with a null
// client_connector_ instead of runtime_error_request_rejected when make_client_connector fails.
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

// Catches calling subscribe_event while the remote service is not_available, or failing to
// subscribe every configured Event_id in Event_mode::update once it becomes available.
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

// Catches aborting on maybe_sample.value() or calling Send when ipc_event.Allocate() fails because
// no LoLa sample slot is free.
TEST_F(RemoteServiceInstanceTest, EventUpdateSkipsWhenSampleAllocationFails) {
    auto const cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    });
    auto const connected = CreateConnectedInstance(cfg, {kEventSerialized});
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

// Catches publishing an uninitialized or corrupt LoLa sample via ipc_event.Send when
// score_com_serializer_deserialize rejects a malformed or oversized SOME/IP payload.
TEST_F(RemoteServiceInstanceTest, EventUpdateSkipsWhenPayloadDeserializationFails) {
    auto const cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    });
    auto const connected = CreateConnectedInstance(cfg, {kEventSerialized});
    PreSerializedData<kMaxMessageSize> sample_storage{};

    EXPECT_CALL(*connected.serialized_event, Allocate(_))
        .WillOnce([&sample_storage](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeAllocateeSample(&sample_storage, std::move(guard));
        });
    EXPECT_CALL(*connected.serialized_event, Send(_, _)).Times(0);

    std::vector<std::byte> const oversized_wire(kOversizedWirePacketSize, kCorruptFillByte);
    connected.callbacks.on_event_update(*connected.client_connector, kSerializedSocomId,
                                        MakeSpanPayload(score::cpp::span<std::byte const>{
                                            oversized_wire.data(), oversized_wire.size()}));
}

// Catches calling .value() on ipc_event.Send() and crashing gatewayd when LoLa Send fails.
TEST_F(RemoteServiceInstanceTest, EventUpdateHandlesSkeletonSendFailureGracefully) {
    auto const cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    });
    auto const connected = CreateConnectedInstance(cfg, {kEventSerialized});
    PreSerializedData<kMaxMessageSize> sample_storage{};
    auto const valid_wire = MakeValidWireBytes();

    EXPECT_CALL(*connected.serialized_event, Allocate(_))
        .WillOnce([&sample_storage](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeAllocateeSample(&sample_storage, std::move(guard));
        });
    EXPECT_CALL(*connected.serialized_event, Send(_, _))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));

    connected.callbacks.on_event_update(
        *connected.client_connector, kSerializedSocomId,
        MakeSpanPayload(score::cpp::span<std::byte const>{valid_wire.data(), valid_wire.size()}));
}

// SOCom delivers the full SOME/IP frame, so forgetting to strip kSomeipFullHeaderSize would copy
// the 16-byte header into the LoLa sample instead of sending only the event payload body.
TEST_F(RemoteServiceInstanceTest, EventUpdateDeserializesSomeipPacketAndSendsSample) {
    auto const cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    });
    auto const connected = CreateConnectedInstance(cfg, {kEventSerialized});
    PreSerializedData<kMaxMessageSize> sample_storage{};
    auto const valid_wire = MakeValidWireBytes();

    EXPECT_CALL(*connected.serialized_event, Allocate(_))
        .WillOnce([&sample_storage](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeAllocateeSample(&sample_storage, std::move(guard));
        });
    EXPECT_CALL(*connected.serialized_event, Send(_, _)).WillOnce(Return(score::Result<void>{}));

    connected.callbacks.on_event_update(
        *connected.client_connector, kSerializedSocomId,
        MakeSpanPayload(score::cpp::span<std::byte const>{valid_wire.data(), valid_wire.size()}));

    EXPECT_EQ(sample_storage.size, kWirePayloadBodySize);
    EXPECT_TRUE(std::equal(kWirePayloadBytes.cbegin(), kWirePayloadBytes.cend(),
                           std::cbegin(sample_storage.data)));
}

// Catches dereferencing ipc_skeleton_.GetEvents().cend() in forward_event when the configured event
// name is absent from the LoLa skeleton's event map.
TEST_F(RemoteServiceInstanceTest, EventUpdateDropsEventWhenMissingFromIpcSkeleton) {
    auto cfg = BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    });
    auto const connected = CreateConnectedInstance(cfg, {kEventSerialized});
    auto const valid_wire = MakeValidWireBytes();

    auto const it = std::search(cfg.buffer->begin(), cfg.buffer->end(), kEventSerialized.begin(),
                                kEventSerialized.end());
    ASSERT_NE(it, cfg.buffer->end());
    *it = kCorruptEventNameChar;

    EXPECT_CALL(*connected.serialized_event, Allocate(_)).Times(0);
    connected.callbacks.on_event_update(
        *connected.client_connector, kSerializedSocomId,
        MakeSpanPayload(score::cpp::span<std::byte const>{valid_wire.data(), valid_wire.size()}));
}

}  // namespace
}  // namespace score::someip_gateway::gatewayd
