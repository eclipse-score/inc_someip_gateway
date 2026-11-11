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
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <score/span.hpp>
#include <string_view>
#include <utility>
#include <vector>

#include "score/gateway_ipc_binding/test/unit/mw_com_mocks.hpp"
#include "score/gatewayd/test/service_instance_test_fixture.h"
#include "score/mw/com/com_error_domain.h"
#include "score/mw/com/impl/com_error.h"
#include "score/mw/com/impl/find_service_handle.h"
#include "score/mw/com/impl/find_service_handler.h"
#include "score/mw/com/impl/generic_proxy_event_binding.h"
#include "score/mw/com/impl/handle_type.h"
#include "score/mw/com/impl/instance_specifier.h"
#include "score/mw/com/impl/plumbing/sample_ptr.h"
#include "score/mw/com/impl/scoped_event_receive_handler.h"
#include "score/mw/com/impl/subscription_state.h"
#include "score/mw/com/types.h"
#include "score/result/result.h"
#include "score/serializer/pre_serialized_data.h"
#include "score/socom/error.hpp"
#include "score/socom/payload.hpp"
#include "score/socom/runtime_mock.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/server_connector_mock.hpp"
#include "score/someip/constants.h"

namespace score::someip_gateway::gatewayd {
namespace {

using ::testing::_;
using ::testing::A;
using ::testing::ByMove;
using ::testing::NiceMock;
using ::testing::Return;

using score::someip_gateway::serializer::PreSerializedData;

constexpr std::size_t kFindServiceUid{1U};
constexpr socom::Event_id kSerializedSocomId{0U};
constexpr socom::Method_id kDummyMethodId{0U};
constexpr std::size_t kExpectedInstanceCount{1U};
constexpr std::size_t kSingleSampleProcessed{1U};
constexpr std::size_t kSampleSlotIndex{0U};
constexpr std::size_t kPayloadSize{4U};
constexpr std::size_t kSampleBufferSize{64U};

constexpr std::array<std::byte, kPayloadSize> kTestSampleBytes{
    std::byte{0xDEU},
    std::byte{0xADU},
    std::byte{0xBEU},
    std::byte{0xEFU},
};

constexpr std::uint8_t kSomeipProtocolVersion{1U};
constexpr std::uint8_t kSomeipMessageTypeNotification{0x02U};
constexpr std::uint8_t kSomeipReturnCodeOk{0x00U};
constexpr std::uint8_t kSomeipClientIdByte{0xFFU};
constexpr std::uint8_t kSomeipSessionIdByte{0x00U};
constexpr unsigned int kByteShift8{8U};
constexpr std::uint16_t kLowByteMask{0xFFU};

constexpr std::size_t kSomeipServiceIdHighByteOffset{0U};
constexpr std::size_t kSomeipServiceIdLowByteOffset{1U};
constexpr std::size_t kSomeipMethodIdHighByteOffset{2U};
constexpr std::size_t kSomeipMethodIdLowByteOffset{3U};
constexpr std::size_t kSomeipClientIdHighByteOffset{8U};
constexpr std::size_t kSomeipClientIdLowByteOffset{9U};
constexpr std::size_t kSomeipSessionIdHighByteOffset{10U};
constexpr std::size_t kSomeipSessionIdLowByteOffset{11U};
constexpr std::size_t kSomeipProtocolVersionOffset{12U};
constexpr std::size_t kSomeipInterfaceVersionOffset{13U};
constexpr std::size_t kSomeipMessageTypeOffset{14U};
constexpr std::size_t kSomeipReturnCodeOffset{15U};
constexpr std::size_t kSomeipPayloadOffset{someip::kSomeipFullHeaderSize};

PreSerializedData<kMaxMessageSize> MakeTestSample() noexcept {
    PreSerializedData<kMaxMessageSize> sample{};
    sample.size = kPayloadSize;
    (void)std::copy(kTestSampleBytes.cbegin(), kTestSampleBytes.cend(), std::begin(sample.data));
    return sample;
}

socom::Writable_payload MakeWritablePayload(score::cpp::span<std::byte> const buffer) {
    return socom::Writable_payload{buffer, socom::kNoSlotHandle, []() {}};
}

void ExpectSingleSampleDelivered(
    NiceMock<score::mw::com::impl::mock_binding::GenericProxyEvent>& event_mock,
    PreSerializedData<kMaxMessageSize>& sample_obj) {
    EXPECT_CALL(event_mock, GetNewSamples(_, _))
        .WillOnce(
            [&sample_obj](
                score::mw::com::impl::GenericProxyEventBinding::Callback&& cb,
                score::mw::com::impl::TrackerGuardFactory& tracker) -> score::Result<std::size_t> {
                cb(score::gateway_ipc_binding::testing::MakeMockSample(&sample_obj, tracker),
                   kSampleSlotIndex);
                return kSingleSampleProcessed;
            });
}

void VerifySomeipNotificationPacket(score::cpp::span<std::byte const> const wire_bytes) {
    ASSERT_EQ(wire_bytes.size(), someip::kSomeipFullHeaderSize + kPayloadSize);
    EXPECT_EQ(wire_bytes[kSomeipServiceIdHighByteOffset],
              static_cast<std::byte>(kServiceId >> kByteShift8));
    EXPECT_EQ(wire_bytes[kSomeipServiceIdLowByteOffset],
              static_cast<std::byte>(kServiceId & kLowByteMask));
    EXPECT_EQ(wire_bytes[kSomeipMethodIdHighByteOffset],
              static_cast<std::byte>(kEventSerializedId >> kByteShift8));
    EXPECT_EQ(wire_bytes[kSomeipMethodIdLowByteOffset],
              static_cast<std::byte>(kEventSerializedId & kLowByteMask));
    EXPECT_EQ(wire_bytes[kSomeipClientIdHighByteOffset], std::byte{kSomeipClientIdByte});
    EXPECT_EQ(wire_bytes[kSomeipClientIdLowByteOffset], std::byte{kSomeipClientIdByte});
    EXPECT_EQ(wire_bytes[kSomeipSessionIdHighByteOffset], std::byte{kSomeipSessionIdByte});
    EXPECT_EQ(wire_bytes[kSomeipSessionIdLowByteOffset], std::byte{kSomeipSessionIdByte});
    EXPECT_EQ(wire_bytes[kSomeipProtocolVersionOffset], std::byte{kSomeipProtocolVersion});
    EXPECT_EQ(wire_bytes[kSomeipInterfaceVersionOffset], std::byte{kServiceMajorVersion});
    EXPECT_EQ(wire_bytes[kSomeipMessageTypeOffset], std::byte{kSomeipMessageTypeNotification});
    EXPECT_EQ(wire_bytes[kSomeipReturnCodeOffset], std::byte{kSomeipReturnCodeOk});
    for (std::size_t i{0U}; i < kPayloadSize; ++i) {
        EXPECT_EQ(wire_bytes[kSomeipPayloadOffset + i], kTestSampleBytes[i]);
    }
}

class LocalServiceInstanceTest : public ServiceInstanceTestFixture {
   protected:
    struct ReceiveHandlerContext {
        std::unique_ptr<LocalServiceInstance> instance{};
        NiceMock<score::mw::com::impl::mock_binding::GenericProxyEvent>* event_mock{nullptr};
        NiceMock<socom::Server_connector_mock>* sc_mock{nullptr};
        std::weak_ptr<score::mw::com::impl::ScopedEventReceiveHandler> receive_handler{};
    };

    void ExpectMakeServerConnector(
        NiceMock<socom::Server_connector_mock>** const out_sc_mock = nullptr,
        socom::Disabled_server_connector::Callbacks* const out_callbacks = nullptr) {
        EXPECT_CALL(socom_runtime_, make_server_connector(_, _, _))
            .WillOnce([out_sc_mock, out_callbacks](
                          socom::Server_service_interface_definition, socom::Service_instance,
                          socom::Disabled_server_connector::Callbacks callbacks)
                          -> score::Result<socom::Disabled_server_connector::Uptr> {
                if (out_callbacks != nullptr) {
                    *out_callbacks = std::move(callbacks);
                }
                auto sc_mock = std::make_unique<NiceMock<socom::Server_connector_mock>>();
                auto* const sc_ptr = sc_mock.get();
                ON_CALL(*sc_ptr, enable()).WillByDefault(Return(sc_ptr));
                if (out_sc_mock != nullptr) {
                    *out_sc_mock = sc_ptr;
                }
                return sc_mock;
            });
    }

    NiceMock<score::mw::com::impl::mock_binding::GenericProxyEvent>* ExpectProxyWithSingleEvent(
        std::string_view const event_name = kEventSerialized) {
        auto proxy_mock = std::make_unique<NiceMock<score::mw::com::impl::mock_binding::Proxy>>();
        auto event_mock =
            std::make_unique<NiceMock<score::mw::com::impl::mock_binding::GenericProxyEvent>>();
        auto* const event_ptr = event_mock.get();
        ON_CALL(*event_ptr, GetSubscriptionState())
            .WillByDefault(Return(score::mw::com::impl::SubscriptionState::kNotSubscribed));
        EXPECT_CALL(*proxy_mock, IsEventProvided(event_name)).WillOnce(Return(true));
        EXPECT_CALL(proxy_binding_factory_mock(), Create(_))
            .WillOnce(Return(ByMove(std::move(proxy_mock))));
        EXPECT_CALL(generic_proxy_event_factory_mock(), Create(_, _, event_name, _))
            .WillOnce(Return(ByMove(std::move(event_mock))));
        return event_ptr;
    }

    score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>
    StartAsyncDiscoveryAndCaptureHandler() {
        return StartAsyncDiscoveryAndCaptureHandler(default_cfg_);
    }

    score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>
    StartAsyncDiscoveryAndCaptureHandler(BuiltConfig const& cfg) {
        score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType> find_handler{};
        EXPECT_CALL(service_discovery_mock(),
                    StartFindService(_, A<score::mw::com::impl::InstanceSpecifier>()))
            .WillOnce([&find_handler](
                          score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>
                              handler,
                          score::mw::com::impl::InstanceSpecifier)
                          -> score::Result<score::mw::com::impl::FindServiceHandle> {
                find_handler = std::move(handler);
                return score::mw::com::impl::make_FindServiceHandle(kFindServiceUid);
            });
        auto const handle_res = LocalServiceInstance::CreateAsyncLocalServices(
            cfg.local_instance, cfg.service_type, socom_runtime_, instances_);
        EXPECT_TRUE(handle_res.has_value());
        return find_handler;
    }

    ReceiveHandlerContext CreateInstanceWithReceiveHandler() {
        return CreateInstanceWithReceiveHandler(default_cfg_);
    }

    ReceiveHandlerContext CreateInstanceWithReceiveHandler(BuiltConfig const& cfg) {
        ReceiveHandlerContext ctx{};
        ctx.event_mock = ExpectProxyWithSingleEvent(kEventSerialized);
        auto proxy_res =
            score::mw::com::GenericProxy::Create(MakeHandle(kLocalSpecifier, {kEventSerialized}));
        EXPECT_TRUE(proxy_res.has_value());

        ExpectMakeServerConnector(&ctx.sc_mock);
        EXPECT_CALL(*ctx.event_mock, SetReceiveHandler(_))
            .WillOnce([&ctx](std::weak_ptr<score::mw::com::impl::ScopedEventReceiveHandler> handler)
                          -> score::Result<void> {
                ctx.receive_handler = std::move(handler);
                return {};
            });
        EXPECT_CALL(*ctx.event_mock, Subscribe(_)).WillOnce(Return(score::Result<void>{}));

        auto inst_res = LocalServiceInstance::Create(cfg.local_instance, cfg.service_type,
                                                     std::move(*proxy_res), socom_runtime_);
        EXPECT_TRUE(inst_res.has_value());
        ctx.instance = std::move(*inst_res);
        return ctx;
    }

    std::vector<std::unique_ptr<LocalServiceInstance>> instances_{};
};

// Returns ComErrc::kInvalidConfiguration when service_instance_config is null.
TEST_F(LocalServiceInstanceTest, CreateAsyncLocalServicesFailsWhenInstanceConfigIsNull) {
    auto const null_res = LocalServiceInstance::CreateAsyncLocalServices(
        nullptr, default_cfg_.service_type, socom_runtime_, instances_);
    ASSERT_FALSE(null_res.has_value());
    EXPECT_EQ(null_res.error(), score::mw::com::ComErrc::kInvalidConfiguration);
}

// Returns the error from GenericProxy::StartFindService() when service discovery fails to start.
TEST_F(LocalServiceInstanceTest, CreateAsyncLocalServicesPropagatesStartFindServiceFailure) {
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, A<score::mw::com::impl::InstanceSpecifier>()))
        .WillOnce(Return(
            score::MakeUnexpected(score::mw::com::impl::ComErrc::kFindServiceHandlerFailure)));

    auto const find_fail = LocalServiceInstance::CreateAsyncLocalServices(
        default_cfg_.local_instance, default_cfg_.service_type, socom_runtime_, instances_);
    EXPECT_FALSE(find_fail.has_value());
}

// Does not create a SOCom Server_connector or LocalServiceInstance when GenericProxy::Create()
// fails in the discovery callback.
TEST_F(LocalServiceInstanceTest, FindServiceCallbackReturnsEarlyWhenProxyCreationFails) {
    auto find_handler = StartAsyncDiscoveryAndCaptureHandler();
    score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType> const handles{
        MakeHandle(kLocalSpecifier, {kEventSerialized})};

    EXPECT_CALL(proxy_binding_factory_mock(), Create(_)).WillOnce(Return(ByMove(nullptr)));
    find_handler(handles, score::mw::com::impl::make_FindServiceHandle(kFindServiceUid));
    EXPECT_TRUE(instances_.empty());
}

// Does not store a LocalServiceInstance when SOCom make_server_connector() fails in the discovery
// callback.
TEST_F(LocalServiceInstanceTest, FindServiceCallbackReturnsEarlyWhenServerConnectorCreationFails) {
    auto find_handler = StartAsyncDiscoveryAndCaptureHandler();
    score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType> const handles{
        MakeHandle(kLocalSpecifier, {kEventSerialized})};

    (void)ExpectProxyWithSingleEvent(kEventSerialized);
    EXPECT_CALL(socom_runtime_, make_server_connector(_, _, _))
        .WillOnce(Return(ByMove(score::MakeUnexpected<socom::Disabled_server_connector::Uptr>(
            socom::Construction_error::duplicate_service))));

    find_handler(handles, score::mw::com::impl::make_FindServiceHandle(kFindServiceUid));
    EXPECT_TRUE(instances_.empty());
}

// Stores the created LocalServiceInstance and calls StopFindService(), keeping the instance even
// if StopFindService() returns an error.
TEST_F(LocalServiceInstanceTest,
       FindServiceCallbackStoresCreatedInstanceAndLogsStopFindServiceError) {
    auto find_handler = StartAsyncDiscoveryAndCaptureHandler();
    score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType> const handles{
        MakeHandle(kLocalSpecifier, {kEventSerialized})};

    auto* const event_mock = ExpectProxyWithSingleEvent(kEventSerialized);
    EXPECT_CALL(*event_mock, SetReceiveHandler(_)).WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*event_mock, Subscribe(_)).WillOnce(Return(score::Result<void>{}));
    ExpectMakeServerConnector();
    EXPECT_CALL(service_discovery_mock(), StopFindService(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kInvalidHandle)));

    find_handler(handles, score::mw::com::impl::make_FindServiceHandle(kFindServiceUid));
    EXPECT_EQ(instances_.size(), kExpectedInstanceCount);
}

// Skips configured events missing from GenericProxy::GetEvents() or missing a registered
// serializer without failing service creation.
TEST_F(LocalServiceInstanceTest, CreateSkipsMissingIpcEventAndUnconfiguredSerializerEvent) {
    auto const cfg = BuildConfig({
        EventSpec{kEventMissingInIpc, kEventMissingInIpcId, kMaxMessageSize},
        EventSpec{kEventNoSerializer, kEventNoSerializerId, std::nullopt},
    });
    auto* const no_ser_event = ExpectProxyWithSingleEvent(kEventNoSerializer);
    EXPECT_CALL(*no_ser_event, SetReceiveHandler(_)).Times(0);
    EXPECT_CALL(*no_ser_event, Subscribe(_)).Times(0);

    auto proxy_res =
        score::mw::com::GenericProxy::Create(MakeHandle(kLocalSpecifier, {kEventNoSerializer}));
    ASSERT_TRUE(proxy_res.has_value());

    ExpectMakeServerConnector();
    auto const inst_res = LocalServiceInstance::Create(cfg.local_instance, cfg.service_type,
                                                       std::move(*proxy_res), socom_runtime_);
    EXPECT_TRUE(inst_res.has_value());
}

// Logs SetReceiveHandler() and Subscribe() errors on an event without failing
// LocalServiceInstance::Create() for the whole service.
TEST_F(LocalServiceInstanceTest, CreateLogsErrorsWhenSetReceiveHandlerAndSubscribeFail) {
    auto* const event_mock = ExpectProxyWithSingleEvent(kEventSerialized);
    EXPECT_CALL(*event_mock, SetReceiveHandler(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kSetHandlerNotSet)));
    EXPECT_CALL(*event_mock, Subscribe(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));

    auto proxy_res =
        score::mw::com::GenericProxy::Create(MakeHandle(kLocalSpecifier, {kEventSerialized}));
    ASSERT_TRUE(proxy_res.has_value());

    ExpectMakeServerConnector();
    auto const inst_res =
        LocalServiceInstance::Create(default_cfg_.local_instance, default_cfg_.service_type,
                                     std::move(*proxy_res), socom_runtime_);
    EXPECT_TRUE(inst_res.has_value());
}

// Registers non-empty SOCom Server_connector callbacks that reject unsupported SOME/IP method
// calls and ignore event subscription or update-request callbacks.
TEST_F(LocalServiceInstanceTest,
       DefaultServerConnectorCallbacksRejectMethodCallsAndIgnoreEventNotifications) {
    auto* const event_mock = ExpectProxyWithSingleEvent(kEventSerialized);
    EXPECT_CALL(*event_mock, SetReceiveHandler(_)).WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*event_mock, Subscribe(_)).WillOnce(Return(score::Result<void>{}));

    auto proxy_res =
        score::mw::com::GenericProxy::Create(MakeHandle(kLocalSpecifier, {kEventSerialized}));
    ASSERT_TRUE(proxy_res.has_value());

    socom::Disabled_server_connector::Callbacks captured_callbacks{};
    ExpectMakeServerConnector(nullptr, &captured_callbacks);
    auto const inst_res =
        LocalServiceInstance::Create(default_cfg_.local_instance, default_cfg_.service_type,
                                     std::move(*proxy_res), socom_runtime_);
    ASSERT_TRUE(inst_res.has_value());

    NiceMock<socom::Server_connector_mock> dummy_sc{};
    EXPECT_EQ(captured_callbacks.on_method_call(dummy_sc, kDummyMethodId, socom::empty_payload(),
                                                std::nullopt),
              nullptr);
    captured_callbacks.on_event_subscription_change(dummy_sc, kSerializedSocomId,
                                                    socom::Event_state::subscribed);
    captured_callbacks.on_event_update_request(dummy_sc, kSerializedSocomId);
    auto const method_alloc =
        captured_callbacks.on_method_call_payload_allocate(dummy_sc, kDummyMethodId);
    ASSERT_FALSE(method_alloc.has_value());
    EXPECT_EQ(method_alloc.error(), socom::Error::runtime_error_request_rejected);
}

// Logs and returns without aborting when GetNewSamples() fails inside the LoLa receive handler.
TEST_F(LocalServiceInstanceTest, ReceiveHandlerLogsErrorWhenGetNewSamplesFails) {
    auto const ctx = CreateInstanceWithReceiveHandler();
    ASSERT_FALSE(ctx.receive_handler.expired());

    EXPECT_CALL(*ctx.event_mock, GetNewSamples(_, _))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    (*ctx.receive_handler.lock())();
}

// Skips serialization and update_event() when SOCom allocate_event_payload() returns an error.
TEST_F(LocalServiceInstanceTest, ReceiveHandlerSkipsUpdateWhenPayloadAllocationFails) {
    auto const ctx = CreateInstanceWithReceiveHandler();
    ASSERT_FALSE(ctx.receive_handler.expired());

    auto sample_obj = MakeTestSample();
    ExpectSingleSampleDelivered(*ctx.event_mock, sample_obj);
    EXPECT_CALL(*ctx.sc_mock, allocate_event_payload(kSerializedSocomId))
        .WillOnce(Return(ByMove(score::MakeUnexpected<socom::Writable_payload>(
            socom::Server_connector_error::logic_error_id_out_of_range))));
    EXPECT_CALL(*ctx.sc_mock, update_event(_, _)).Times(0);

    (*ctx.receive_handler.lock())();
}

// Subtracts the 16-byte SOME/IP header from the buffer capacity passed to the serializer and skips
// update_event() when serialization fails.
TEST_F(LocalServiceInstanceTest,
       ReceiveHandlerSkipsUpdateWhenAllocatedBufferIsTooSmallForSerializer) {
    auto const ctx = CreateInstanceWithReceiveHandler();
    ASSERT_FALSE(ctx.receive_handler.expired());

    auto sample_obj = MakeTestSample();
    std::array<std::byte, someip::kSomeipFullHeaderSize> header_only_buf{};
    ExpectSingleSampleDelivered(*ctx.event_mock, sample_obj);
    EXPECT_CALL(*ctx.sc_mock, allocate_event_payload(kSerializedSocomId))
        .WillOnce(Return(ByMove(MakeWritablePayload(
            score::cpp::span<std::byte>{header_only_buf.data(), header_only_buf.size()}))));
    EXPECT_CALL(*ctx.sc_mock, update_event(_, _)).Times(0);

    (*ctx.receive_handler.lock())();
}

// Logs and returns without aborting when SOCom update_event() returns an error.
TEST_F(LocalServiceInstanceTest, ReceiveHandlerLogsErrorWhenUpdateEventFails) {
    auto const ctx = CreateInstanceWithReceiveHandler();
    ASSERT_FALSE(ctx.receive_handler.expired());

    auto sample_obj = MakeTestSample();
    std::array<std::byte, kSampleBufferSize> full_buf{};
    ExpectSingleSampleDelivered(*ctx.event_mock, sample_obj);
    EXPECT_CALL(*ctx.sc_mock, allocate_event_payload(kSerializedSocomId))
        .WillOnce(Return(ByMove(
            MakeWritablePayload(score::cpp::span<std::byte>{full_buf.data(), full_buf.size()}))));
    EXPECT_CALL(*ctx.sc_mock, update_event(kSerializedSocomId, _))
        .WillOnce(Return(
            score::MakeUnexpected(socom::Server_connector_error::logic_error_id_out_of_range)));

    (*ctx.receive_handler.lock())();
}

// Writes the 16-byte big-endian SOME/IP notification header followed by the serialized payload,
// and calls shrink() with the written byte count.
TEST_F(LocalServiceInstanceTest,
       ReceiveHandlerSerializesSampleIntoSomeipPacketAndUpdatesServerConnector) {
    auto const ctx = CreateInstanceWithReceiveHandler();
    ASSERT_FALSE(ctx.receive_handler.expired());

    auto sample_obj = MakeTestSample();
    std::array<std::byte, kSampleBufferSize> full_buf{};
    ExpectSingleSampleDelivered(*ctx.event_mock, sample_obj);
    EXPECT_CALL(*ctx.sc_mock, allocate_event_payload(kSerializedSocomId))
        .WillOnce(Return(ByMove(
            MakeWritablePayload(score::cpp::span<std::byte>{full_buf.data(), full_buf.size()}))));

    std::vector<std::byte> captured_wire_bytes{};
    EXPECT_CALL(*ctx.sc_mock, update_event(kSerializedSocomId, _))
        .WillOnce(
            [&captured_wire_bytes](socom::Event_id, socom::Payload payload) -> score::Result<void> {
                auto const span = payload.data();
                captured_wire_bytes.assign(span.begin(), span.end());
                return {};
            });

    (*ctx.receive_handler.lock())();
    VerifySomeipNotificationPacket(captured_wire_bytes);
}

}  // namespace
}  // namespace score::someip_gateway::gatewayd
