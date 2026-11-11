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

#include "score/gateway_ipc_binding/impl/consumer_service_binding.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/gateway_ipc_binding/impl/sample_layout.hpp"
#include "score/gateway_ipc_binding/impl/service_binding.hpp"
#include "score/gateway_ipc_binding/test/unit/ipc_binding_test_fixture.hpp"
#include "score/memory/data_type_size_info.h"
#include "score/mw/com/impl/com_error.h"
#include "score/mw/com/impl/find_service_handle.h"
#include "score/mw/com/impl/find_service_handler.h"
#include "score/mw/com/impl/generic_proxy_event_binding.h"
#include "score/mw/com/impl/handle_type.h"
#include "score/mw/com/impl/instance_specifier.h"
#include "score/mw/com/impl/plumbing/sample_ptr.h"
#include "score/mw/com/impl/sample_reference_tracker.h"
#include "score/mw/com/impl/scoped_event_receive_handler.h"
#include "score/mw/com/impl/subscription_state.h"
#include "score/result/result.h"
#include "score/socom/error.hpp"
#include "score/socom/payload.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/server_connector_mock.hpp"

namespace score::gateway_ipc_binding::mw_com {
namespace {

using ::testing::_;
using ::testing::A;
using ::testing::ByMove;
using ::testing::NiceMock;
using ::testing::Return;

constexpr socom::Method_id kDefaultMethodId{0U};
constexpr socom::Event_id kFirstEventId{0U};
constexpr socom::Event_id kUnknownEventId{99U};
constexpr std::size_t kFirstFindServiceUid{1U};
constexpr std::size_t kShortSampleSize{8U};
constexpr std::size_t kValidSampleSize{kSample_prefix_size + kEvent0MaxPayload};
constexpr std::size_t kSampleBufferSize{32U};
constexpr std::size_t kValidPayloadLength{4U};
constexpr std::size_t kOversizedPayloadLength{kEvent0MaxPayload + 4U};
constexpr std::size_t kCallbackMaxSampleCount{0U};
constexpr std::size_t kProcessedSampleCount{4U};
constexpr std::byte kSamplePayloadByte{0x42};

class ConsumerServiceBindingTest : public IpcBindingTestFixture {
   protected:
    struct CreatedConsumer {
        std::unique_ptr<Consumer_service_binding> binding{};
        socom::Disabled_server_connector::Callbacks callbacks{};
        NiceMock<socom::Server_connector_mock>* connector_mock{nullptr};
    };

    struct StartedConsumer {
        std::unique_ptr<Consumer_service_binding> binding{};
        socom::Disabled_server_connector::Callbacks callbacks{};
        NiceMock<socom::Server_connector_mock>* connector_mock{nullptr};
        score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType> find_handler{};
    };

    struct SubscribedEvent {
        NiceMock<testing::GenericProxyEvent>* event_mock{nullptr};
        std::weak_ptr<score::mw::com::impl::ScopedEventReceiveHandler> receive_handler{};
    };

    CreatedConsumer CreateConsumer() {
        CreatedConsumer created{};
        created.connector_mock = ExpectServerConnectorCreated(&created.callbacks);
        auto binding_res =
            Consumer_service_binding::create(socom_runtime_, MakeServiceConfig(Role::consumer));
        EXPECT_TRUE(binding_res.has_value());
        created.binding = std::move(*binding_res);
        return created;
    }

    StartedConsumer CreateAndStartConsumer() {
        auto created = CreateConsumer();
        StartedConsumer started{
            std::move(created.binding), std::move(created.callbacks), created.connector_mock, {}};
        EXPECT_CALL(service_discovery_mock(),
                    StartFindService(_, A<score::mw::com::impl::InstanceSpecifier>()))
            .WillOnce([&started](
                          score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>
                              handler,
                          score::mw::com::impl::InstanceSpecifier)
                          -> score::Result<score::mw::com::impl::FindServiceHandle> {
                started.find_handler = std::move(handler);
                return score::mw::com::impl::make_FindServiceHandle(kFirstFindServiceUid);
            });
        EXPECT_TRUE(started.binding->start().has_value());
        return started;
    }

    using MwComTestFixture::ExpectProxyWithEvent;

    NiceMock<testing::GenericProxyEvent>* ExpectProxyWithEvent(
        std::size_t const peer_sample_size = kValidSampleSize) {
        auto* const event_mock_ptr = MwComTestFixture::ExpectProxyWithEvent(kEvent0);
        ON_CALL(*event_mock_ptr, GetDataTypeSizeInfo())
            .WillByDefault(
                Return(score::memory::DataTypeSizeInfo{peer_sample_size, kSample_alignment}));
        return event_mock_ptr;
    }

    SubscribedEvent AttachAndSubscribeConsumer(StartedConsumer& started) {
        SubscribedEvent sub{};
        sub.event_mock = ExpectProxyWithEvent();
        EXPECT_CALL(*sub.event_mock, Subscribe(_)).WillOnce(Return(score::Result<void>{}));
        EXPECT_CALL(*sub.event_mock, SetReceiveHandler(_))
            .WillOnce([&sub](std::weak_ptr<score::mw::com::impl::ScopedEventReceiveHandler> handler)
                          -> score::Result<void> {
                sub.receive_handler = std::move(handler);
                return {};
            });
        started.callbacks.on_event_subscription_change(*started.connector_mock, kFirstEventId,
                                                       socom::Event_state::subscribed);
        score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType> const
            handles{MakeHandle()};
        started.find_handler(handles,
                             score::mw::com::impl::make_FindServiceHandle(kFirstFindServiceUid));
        return sub;
    }
};

// Invalid specifier syntax fails in create() before creating a SOCom Server_connector.
TEST_F(ConsumerServiceBindingTest, CreateRejectsInvalidInstanceSpecifier) {
    auto const result = Consumer_service_binding::create(
        socom_runtime_, MakeServiceConfig(Role::consumer, kInvalidSpecifier));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::logic_error_invalid_instance_specifier);
}

// Undeployed specifier fails in create() instead of waiting indefinitely in StartFindService().
TEST_F(ConsumerServiceBindingTest, CreateRejectsUndeployedInstanceSpecifier) {
    StubInstanceNotResolved();
    auto const result =
        Consumer_service_binding::create(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::logic_error_invalid_instance_specifier);
}

// Returns runtime_error_service_setup_failed when SOCom make_server_connector() fails.
TEST_F(ConsumerServiceBindingTest, CreateFailsWhenServerConnectorCreationFails) {
    EXPECT_CALL(socom_runtime_, make_server_connector(_, _, _))
        .WillOnce(Return(ByMove(score::MakeUnexpected<socom::Disabled_server_connector::Uptr>(
            socom::Construction_error::duplicate_service))));
    auto const result =
        Consumer_service_binding::create(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::runtime_error_service_setup_failed);
}

// Unsupported SOCom method and update-request callbacks are no-ops, and out-of-range event IDs are
// ignored without out-of-bounds access.
TEST_F(ConsumerServiceBindingTest,
       ServerConnectorDefaultCallbacksHandleUnsupportedOperationsAndOutOfRangeEvent) {
    socom::Disabled_server_connector::Callbacks captured_cbs{};
    (void)ExpectServerConnectorCreated(&captured_cbs);

    auto const binding_res =
        make_service_binding(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_TRUE(binding_res.has_value());

    NiceMock<socom::Server_connector_mock> dummy_sc{};
    EXPECT_EQ(captured_cbs.on_method_call(dummy_sc, kDefaultMethodId, socom::empty_payload(),
                                          std::nullopt),
              nullptr);
    captured_cbs.on_event_update_request(dummy_sc, kFirstEventId);
    auto const alloc_res = captured_cbs.on_method_call_payload_allocate(dummy_sc, kDefaultMethodId);
    EXPECT_TRUE(alloc_res.has_value());
    captured_cbs.on_event_subscription_change(dummy_sc, kUnknownEventId,
                                              socom::Event_state::subscribed);
}

// Returns runtime_error_service_find_failed when LoLa StartFindService() fails.
TEST_F(ConsumerServiceBindingTest, StartFailsWhenStartFindServiceFails) {
    auto created = CreateConsumer();
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, A<score::mw::com::impl::InstanceSpecifier>()))
        .WillOnce(Return(
            score::MakeUnexpected(score::mw::com::impl::ComErrc::kFindServiceHandlerFailure)));
    auto const start_res = created.binding->start();
    ASSERT_FALSE(start_res.has_value());
    EXPECT_EQ(start_res.error(), Mw_com_binding_error::runtime_error_service_find_failed);
}

// noexcept destructor logs and continues instead of aborting if StopFindService() returns an error.
TEST_F(ConsumerServiceBindingTest, DestructorHandlesStopFindServiceFailureGracefully) {
    auto started = CreateAndStartConsumer();
    EXPECT_CALL(service_discovery_mock(), StopFindService(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kInvalidHandle)));
    started.binding.reset();
}

// Empty discovery results or a failed GenericProxy::Create() keep the SOCom Server_connector
// disabled.
TEST_F(ConsumerServiceBindingTest, FindServiceCallbackHandlesEmptyHandlesAndProxyCreationFailure) {
    auto started = CreateAndStartConsumer();
    started.callbacks.on_event_subscription_change(*started.connector_mock, kFirstEventId,
                                                   socom::Event_state::subscribed);
    started.find_handler(
        score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType>{},
        score::mw::com::impl::make_FindServiceHandle(kFirstFindServiceUid));

    score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType> const handles{
        MakeHandle()};
    EXPECT_CALL(proxy_binding_factory_mock(), Create(_)).WillOnce(Return(ByMove(nullptr)));
    started.find_handler(handles,
                         score::mw::com::impl::make_FindServiceHandle(kFirstFindServiceUid));
}

// Rejects a GenericProxy with a missing event or undersized SHM slot so a later StartFindService()
// callback can create a new GenericProxy.
TEST_F(ConsumerServiceBindingTest,
       FindServiceCallbackRejectsProxyWithMissingEventOrUndersizedSample) {
    auto started = CreateAndStartConsumer();
    score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType> const handles{
        MakeHandle()};

    EXPECT_CALL(proxy_binding_factory_mock(), Create(_))
        .WillOnce(Return(ByMove(std::make_unique<NiceMock<testing::Proxy>>())));
    started.find_handler(handles,
                         score::mw::com::impl::make_FindServiceHandle(kFirstFindServiceUid));

    (void)ExpectProxyWithEvent(kShortSampleSize);
    started.find_handler(handles,
                         score::mw::com::impl::make_FindServiceHandle(kFirstFindServiceUid));
}

// Subscribes in LoLa for events subscribed on SOCom before service discovery, and ignores
// duplicate StartFindService() callbacks while a GenericProxy is active.
TEST_F(ConsumerServiceBindingTest,
       AttachReconcilesPendingSubscriptionAndIgnoresDuplicateFindServiceHandle) {
    auto started = CreateAndStartConsumer();
    auto* const event_mock_ptr = ExpectProxyWithEvent();
    started.callbacks.on_event_subscription_change(*started.connector_mock, kFirstEventId,
                                                   socom::Event_state::subscribed);

    score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType> const handles{
        MakeHandle()};
    EXPECT_CALL(*event_mock_ptr, Subscribe(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    started.find_handler(handles,
                         score::mw::com::impl::make_FindServiceHandle(kFirstFindServiceUid));

    started.find_handler(handles,
                         score::mw::com::impl::make_FindServiceHandle(kFirstFindServiceUid));
}

// Calls Unsubscribe() if SetReceiveHandler() fails after Subscribe() (LoLa aborts if GenericProxy
// is destroyed while an event is still subscribed).
TEST_F(ConsumerServiceBindingTest, SubscriptionRollsBackWhenSetReceiveHandlerFails) {
    auto started = CreateAndStartConsumer();
    auto* const event_mock_ptr = ExpectProxyWithEvent();
    score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType> const handles{
        MakeHandle()};
    started.find_handler(handles,
                         score::mw::com::impl::make_FindServiceHandle(kFirstFindServiceUid));

    EXPECT_CALL(*event_mock_ptr, Subscribe(_)).WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*event_mock_ptr, SetReceiveHandler(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kSetHandlerNotSet)));
    EXPECT_CALL(*event_mock_ptr, GetSubscriptionState())
        .WillOnce(Return(score::mw::com::impl::SubscriptionState::kNotSubscribed))
        .WillOnce(Return(score::mw::com::impl::SubscriptionState::kSubscribed));
    EXPECT_CALL(*event_mock_ptr, Unsubscribe());
    started.callbacks.on_event_subscription_change(*started.connector_mock, kFirstEventId,
                                                   socom::Event_state::subscribed);
}

// Subscribes and unsubscribes on LoLa as SOCom state changes, and unsubscribes before destroying
// GenericProxy when the LoLa service stops offering.
TEST_F(ConsumerServiceBindingTest, ConnectedConsumerHandlesSubscribeUnsubscribeAndPeerDetach) {
    auto started = CreateAndStartConsumer();
    auto const sub = AttachAndSubscribeConsumer(started);
    ASSERT_FALSE(sub.receive_handler.expired());

    started.callbacks.on_event_subscription_change(*started.connector_mock, kFirstEventId,
                                                   socom::Event_state::subscribed);

    EXPECT_CALL(*sub.event_mock, GetSubscriptionState())
        .WillOnce(Return(score::mw::com::impl::SubscriptionState::kSubscribed));
    EXPECT_CALL(*sub.event_mock, UnsetReceiveHandler()).WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*sub.event_mock, Unsubscribe());
    started.callbacks.on_event_subscription_change(*started.connector_mock, kFirstEventId,
                                                   socom::Event_state::unsubscribed);

    EXPECT_CALL(*sub.event_mock, GetSubscriptionState())
        .WillOnce(Return(score::mw::com::impl::SubscriptionState::kNotSubscribed));
    EXPECT_CALL(*sub.event_mock, Subscribe(_)).WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*sub.event_mock, SetReceiveHandler(_)).WillOnce(Return(score::Result<void>{}));
    started.callbacks.on_event_subscription_change(*started.connector_mock, kFirstEventId,
                                                   socom::Event_state::subscribed);

    EXPECT_CALL(*sub.event_mock, GetSubscriptionState())
        .WillOnce(Return(score::mw::com::impl::SubscriptionState::kSubscribed));
    EXPECT_CALL(*sub.event_mock, UnsetReceiveHandler()).WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*sub.event_mock, Unsubscribe());
    started.find_handler(
        score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType>{},
        score::mw::com::impl::make_FindServiceHandle(kFirstFindServiceUid));
}

// Ignores null SamplePtrs and SHM samples whose 8-byte length prefix exceeds max_payload_size
// instead of reading out of bounds.
TEST_F(ConsumerServiceBindingTest,
       ReceiveHandlerSkipsNullAndOversizedSamplesAndForwardsValidSamples) {
    auto started = CreateAndStartConsumer();
    auto const sub = AttachAndSubscribeConsumer(started);
    ASSERT_FALSE(sub.receive_handler.expired());

    alignas(kSample_alignment) std::array<std::byte, kSampleBufferSize> oversized_buf{};
    write_payload_length(oversized_buf.data(), kOversizedPayloadLength);
    alignas(kSample_alignment) std::array<std::byte, kSampleBufferSize> valid_buf{};
    write_payload_length(valid_buf.data(), kValidPayloadLength);
    valid_buf[kSample_prefix_size] = kSamplePayloadByte;

    EXPECT_CALL(*sub.event_mock, GetNewSamples(_, _))
        .WillOnce(
            [&oversized_buf, &valid_buf](
                score::mw::com::impl::GenericProxyEventBinding::Callback&& cb,
                score::mw::com::impl::TrackerGuardFactory& tracker) -> score::Result<std::size_t> {
                cb(testing::MakeMockSample(nullptr, tracker), kCallbackMaxSampleCount);
                cb(testing::MakeMockSample(oversized_buf.data(), tracker), kCallbackMaxSampleCount);
                cb(testing::MakeMockSample(valid_buf.data(), tracker), kCallbackMaxSampleCount);
                cb(testing::MakeMockSample(valid_buf.data(), tracker), kCallbackMaxSampleCount);
                return kProcessedSampleCount;
            });
    EXPECT_CALL(*started.connector_mock, update_event(kFirstEventId, _))
        .WillOnce(Return(
            score::MakeUnexpected(socom::Server_connector_error::logic_error_id_out_of_range)))
        .WillOnce(Return(score::Result<void>{}));
    (*sub.receive_handler.lock())();
}

// Logs and returns without aborting when GetNewSamples() fails inside the noexcept callback.
TEST_F(ConsumerServiceBindingTest, ReceiveHandlerHandlesGetNewSamplesErrorGracefully) {
    auto started = CreateAndStartConsumer();
    auto const sub = AttachAndSubscribeConsumer(started);
    ASSERT_FALSE(sub.receive_handler.expired());

    EXPECT_CALL(*sub.event_mock, GetNewSamples(_, _))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    (*sub.receive_handler.lock())();
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
