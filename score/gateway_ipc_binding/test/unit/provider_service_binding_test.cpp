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

#include "score/gateway_ipc_binding/impl/provider_service_binding.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/gateway_ipc_binding/impl/sample_layout.hpp"
#include "score/gateway_ipc_binding/test/unit/ipc_binding_test_fixture.hpp"
#include "score/memory/data_type_size_info.h"
#include "score/mw/com/impl/bindings/mock_binding/sample_allocatee_ptr.h"
#include "score/mw/com/impl/com_error.h"
#include "score/mw/com/impl/generic_skeleton_event_binding.h"
#include "score/mw/com/impl/plumbing/sample_allocatee_ptr.h"
#include "score/socom/client_connector.hpp"
#include "score/socom/client_connector_mock.hpp"
#include "score/socom/error.hpp"
#include "score/socom/payload.hpp"
#include "score/socom/runtime_mock.hpp"
#include "score/socom/server_connector.hpp"

namespace score::gateway_ipc_binding::mw_com {
namespace {

using ::testing::_;
using ::testing::ByMove;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

constexpr socom::Event_id kSocomEvent0Id{0U};
constexpr socom::Event_id kUnknownEventId{99U};
constexpr std::size_t kMismatchedEventSize{8U};
constexpr std::size_t kSampleBufferSize{32U};
constexpr std::size_t kShrunkPayloadLengthOnFailure{4U};
constexpr std::size_t kPositivePayloadSize{6U};
constexpr std::uint16_t kZeroMethods{0U};
constexpr std::uint16_t kSingleEventCount{1U};

class ProviderServiceBindingTest : public IpcBindingTestFixture {
   protected:
    std::size_t ExpectedEventSampleSize() const noexcept {
        return sample_size(provider_config_.events.front());
    }

    score::mw::com::impl::mock_binding::GenericSkeletonEvent* ExpectSkeletonWithEvent(
        std::size_t const reported_size, score::Result<void> const register_handler_result = {}) {
        auto skel_mock = std::make_unique<NiceMock<score::mw::com::impl::mock_binding::Skeleton>>();
        skeleton_mock_ = skel_mock.get();
        auto skel_event_mock =
            std::make_unique<NiceMock<score::mw::com::impl::mock_binding::GenericSkeletonEvent>>();
        skeleton_event_mock_ = skel_event_mock.get();

        ON_CALL(*skeleton_event_mock_, GetEventDataTypeSizeInfo())
            .WillByDefault(
                Return(score::memory::DataTypeSizeInfo{reported_size, kSample_alignment}));
        ON_CALL(*skeleton_event_mock_, SetReceiveHandlerRegistrationChangedHandler(_))
            .WillByDefault([this, register_handler_result](
                               score::mw::com::impl::ReceiveHandlerRegistrationChangedCallback cb)
                               -> score::Result<void> {
                interest_cb_ = std::move(cb);
                return register_handler_result;
            });
        ON_CALL(*skeleton_event_mock_, UnsetReceiveHandlerRegistrationChangedHandler())
            .WillByDefault(Return(score::Result<void>{}));

        EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
            .WillOnce(Return(ByMove(std::move(skel_mock))));
        EXPECT_CALL(generic_skeleton_event_factory_mock(), Create(_, _, kEvent0, _))
            .WillOnce(Return(ByMove(std::move(skel_event_mock))));
        return skeleton_event_mock_;
    }

    std::unique_ptr<Provider_service_binding> CreateConnectedProvider() {
        ExpectInstanceResolved();
        (void)ExpectSkeletonWithEvent(ExpectedEventSampleSize());
        EXPECT_CALL(socom_runtime_, make_client_connector(_, _, _))
            .WillOnce([this](socom::Service_interface_definition, socom::Service_instance,
                             socom::Client_connector::Callbacks callbacks)
                          -> score::Result<socom::Client_connector::Uptr> {
                client_callbacks_ = std::move(callbacks);
                auto mock = std::make_unique<NiceMock<socom::Client_connector_mock>>();
                client_connector_mock_ = mock.get();
                return mock;
            });
        auto binding_res = Provider_service_binding::create(socom_runtime_, provider_config_);
        EXPECT_TRUE(binding_res.has_value());
        return std::move(*binding_res);
    }

    void ExpectOfferServiceSucceeds() {
        EXPECT_CALL(*skeleton_mock_, PrepareOffer(_, _, _)).WillOnce(Return(score::Result<void>{}));
        EXPECT_CALL(*skeleton_event_mock_, PrepareOffer(_)).WillOnce(Return(score::Result<void>{}));
        EXPECT_CALL(*skeleton_mock_, VerifyAllMethodHandlersRegistered()).WillOnce(Return(true));
        EXPECT_CALL(service_discovery_mock(), OfferService(_))
            .WillOnce(Return(score::Result<void>{}));
    }

    void OfferService() {
        ExpectOfferServiceSucceeds();
        client_callbacks_.on_service_state_change(
            *client_connector_mock_, socom::Service_state::available, server_interface_definition_);
    }

    static score::Result<score::mw::com::impl::SampleAllocateePtr<void>> MakeAllocatedSample(
        std::byte* const buffer, score::mw::com::impl::SampleAllocateeGuard guard) {
        score::mw::com::impl::mock_binding::SampleAllocateePtr mock_ptr{buffer,
                                                                        [](void*) noexcept {}};
        return score::mw::com::impl::MakeSampleAllocateePtr(std::move(mock_ptr), std::move(guard));
    }

    Service_config const provider_config_{MakeServiceConfig(Role::provider)};
    socom::Server_service_interface_definition const server_interface_definition_{
        provider_config_.interface, socom::to_num_of_methods(kZeroMethods),
        socom::to_num_of_events(kSingleEventCount)};
    StrictMock<socom::Runtime_mock> socom_runtime_{};
    socom::Client_connector::Callbacks client_callbacks_{};
    NiceMock<socom::Client_connector_mock>* client_connector_mock_{nullptr};
    score::mw::com::impl::mock_binding::Skeleton* skeleton_mock_{nullptr};
    score::mw::com::impl::mock_binding::GenericSkeletonEvent* skeleton_event_mock_{nullptr};
    score::mw::com::impl::ReceiveHandlerRegistrationChangedCallback interest_cb_{};
};

// Catches calling .value() on an unchecked InstanceSpecifier or reporting
// runtime_error_service_setup_failed instead of logic_error_invalid_instance_specifier.
TEST_F(ProviderServiceBindingTest, CreateRejectsInvalidInstanceSpecifier) {
    auto const result = Provider_service_binding::create(
        socom_runtime_, MakeServiceConfig(Role::provider, kInvalidSpecifier));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::logic_error_invalid_instance_specifier);
}

// Catches unwrapping a failed GenericSkeleton::Create() result or wiring a SOCom Client_connector
// when the instance is missing from the mw::com deployment.
TEST_F(ProviderServiceBindingTest, CreateFailsWhenInstanceIsNotDeployed) {
    ExpectInstanceNotResolved();
    auto const result = Provider_service_binding::create(socom_runtime_, provider_config_);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::runtime_error_service_setup_failed);
}

// Catches forgetting to pass m_config.events into GenericSkeleton::Create() or ignoring a failed
// LoLa event binding and wiring the SOCom Client_connector anyway.
TEST_F(ProviderServiceBindingTest, CreateFailsWhenEventIsMissingFromSkeleton) {
    ExpectInstanceResolved();
    auto skel_mock = std::make_unique<NiceMock<score::mw::com::impl::mock_binding::Skeleton>>();
    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
        .WillOnce(Return(ByMove(std::move(skel_mock))));
    EXPECT_CALL(generic_skeleton_event_factory_mock(), Create(_, _, kEvent0, _))
        .WillOnce(Return(ByMove(nullptr)));

    auto const result = Provider_service_binding::create(socom_runtime_, provider_config_);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::runtime_error_service_setup_failed);
}

// Catches writing past the end of SHM if LoLa's actual slot size doesn't match sample_size(event).
TEST_F(ProviderServiceBindingTest, CreateFailsWhenSkeletonEventSampleSizeMismatches) {
    ExpectInstanceResolved();
    (void)ExpectSkeletonWithEvent(kMismatchedEventSize);

    auto const result = Provider_service_binding::create(socom_runtime_, provider_config_);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::runtime_error_sample_size_mismatch);
}

// Catches missing LoLa subscriptions when SetReceiveHandlerRegistrationChangedHandler() fails, or
// crashing in the noexcept destructor when unsetting that handler also fails.
TEST_F(ProviderServiceBindingTest, CreateFailsWhenRegisteringSubscriptionHandlerFails) {
    ExpectInstanceResolved();
    auto* const event_mock = ExpectSkeletonWithEvent(
        ExpectedEventSampleSize(),
        score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure));
    EXPECT_CALL(*event_mock, UnsetReceiveHandlerRegistrationChangedHandler())
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));

    auto const result = Provider_service_binding::create(socom_runtime_, provider_config_);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::runtime_error_service_setup_failed);
}

// Catches unwrapping a failed make_client_connector() result or leaving GenericSkeleton callbacks
// registered when SOCom rejects the client connector during setup.
TEST_F(ProviderServiceBindingTest, CreateFailsWhenMakingClientConnectorFails) {
    ExpectInstanceResolved();
    (void)ExpectSkeletonWithEvent(ExpectedEventSampleSize());
    EXPECT_CALL(socom_runtime_, make_client_connector(_, _, _))
        .WillOnce(Return(ByMove(score::MakeUnexpected<socom::Client_connector::Uptr>(
            socom::Construction_error::callback_missing))));

    auto const result = Provider_service_binding::create(socom_runtime_, provider_config_);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::runtime_error_service_setup_failed);
}

// Catches indexing past m_events on an unknown Event_id, leaving on_event_requested_update unset,
// or calling StopOfferService() when not_available arrives for a service that was never offered.
TEST_F(ProviderServiceBindingTest, StartSucceedsAndOutOfRangeOrRequestedCallbacksAreIgnored) {
    auto const binding = CreateConnectedProvider();
    EXPECT_TRUE(binding->start().has_value());

    client_callbacks_.on_event_requested_update(*client_connector_mock_, kSocomEvent0Id,
                                                socom::empty_payload());
    auto const oor_alloc =
        client_callbacks_.on_event_payload_allocate(*client_connector_mock_, kUnknownEventId);
    ASSERT_FALSE(oor_alloc.has_value());
    EXPECT_EQ(oor_alloc.error(), Mw_com_binding_error::logic_error_unknown_event);

    client_callbacks_.on_event_update(*client_connector_mock_, kUnknownEventId,
                                      socom::empty_payload());
    client_callbacks_.on_service_state_change(
        *client_connector_mock_, socom::Service_state::not_available, server_interface_definition_);
}

// Catches losing early LoLa subscriber interest, subscribing on SOCom after OfferService() fails,
// re-offering on duplicate available states, or setting subscribed when subscribe_event() fails.
TEST_F(ProviderServiceBindingTest, ServiceAvailabilityOffersServiceAndReconcilesPeerSubscription) {
    auto const binding = CreateConnectedProvider();
    interest_cb_(true);

    EXPECT_CALL(*skeleton_mock_, PrepareOffer(_, _, _))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    client_callbacks_.on_service_state_change(
        *client_connector_mock_, socom::Service_state::available, server_interface_definition_);
    client_callbacks_.on_service_state_change(
        *client_connector_mock_, socom::Service_state::not_available, server_interface_definition_);

    ExpectOfferServiceSucceeds();
    EXPECT_CALL(*client_connector_mock_, subscribe_event(kSocomEvent0Id, socom::Event_mode::update))
        .WillOnce(Return(score::MakeUnexpected(socom::Error::logic_error_id_out_of_range)));
    client_callbacks_.on_service_state_change(
        *client_connector_mock_, socom::Service_state::available, server_interface_definition_);
    client_callbacks_.on_service_state_change(
        *client_connector_mock_, socom::Service_state::available, server_interface_definition_);

    EXPECT_CALL(*client_connector_mock_, subscribe_event(kSocomEvent0Id, socom::Event_mode::update))
        .WillOnce(Return(score::Result<void>{}));
    interest_cb_(true);
}

// When nobody in LoLa is subscribed, withdrawing the service must destroy and recreate
// GenericSkeleton (LoLa can't re-offer a stopped skeleton) without crashing if recreate fails.
TEST_F(ProviderServiceBindingTest,
       PeerUnsubscriptionAndServiceWithdrawalTearDownAndRecreateSkeleton) {
    auto const binding = CreateConnectedProvider();
    ExpectOfferServiceSucceeds();
    EXPECT_CALL(*client_connector_mock_, subscribe_event(kSocomEvent0Id, socom::Event_mode::update))
        .WillOnce(Return(score::Result<void>{}));
    interest_cb_(true);
    client_callbacks_.on_service_state_change(
        *client_connector_mock_, socom::Service_state::available, server_interface_definition_);

    EXPECT_CALL(*client_connector_mock_, unsubscribe_event(kSocomEvent0Id))
        .WillOnce(Return(score::MakeUnexpected(socom::Error::logic_error_id_out_of_range)));
    interest_cb_(false);

    EXPECT_CALL(service_discovery_mock(), StopOfferService(_))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*skeleton_event_mock_, PrepareStopOffer()).WillOnce(Return());
    EXPECT_CALL(*skeleton_mock_, PrepareStopOffer(_)).WillOnce(Return());
    EXPECT_CALL(*skeleton_event_mock_, UnsetReceiveHandlerRegistrationChangedHandler())
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    client_callbacks_.on_service_state_change(
        *client_connector_mock_, socom::Service_state::not_available, server_interface_definition_);

    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_)).WillOnce(Return(ByMove(nullptr)));
    client_callbacks_.on_service_state_change(
        *client_connector_mock_, socom::Service_state::available, server_interface_definition_);
}

// Catches leaking an SHM slot when the producer drops a Writable_payload without sending, crashing
// when LoLa runs out of slots, or sending an event update not backed by a LoLa sample.
TEST_F(ProviderServiceBindingTest, EventPayloadAllocationHandlesFailureAndDroppedPayloads) {
    auto const binding = CreateConnectedProvider();
    OfferService();

    EXPECT_CALL(*skeleton_event_mock_, Allocate(_))
        .WillOnce(
            Return(ByMove(score::MakeUnexpected<score::mw::com::impl::SampleAllocateePtr<void>>(
                score::mw::com::impl::ComErrc::kSampleAllocationFailure))));
    auto const alloc_fail =
        client_callbacks_.on_event_payload_allocate(*client_connector_mock_, kSocomEvent0Id);
    ASSERT_FALSE(alloc_fail.has_value());
    EXPECT_EQ(alloc_fail.error(), Mw_com_binding_error::runtime_error_sample_allocation_failed);

    alignas(kSample_alignment) std::array<std::byte, kSampleBufferSize> sample_buf{};
    EXPECT_CALL(*skeleton_event_mock_, Allocate(_))
        .WillOnce([&sample_buf](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeAllocatedSample(sample_buf.data(), std::move(guard));
        });
    {
        auto const dropped_alloc =
            client_callbacks_.on_event_payload_allocate(*client_connector_mock_, kSocomEvent0Id);
        ASSERT_TRUE(dropped_alloc.has_value());
    }
    client_callbacks_.on_event_update(*client_connector_mock_, kSocomEvent0Id,
                                      socom::empty_payload());
}

// Catches stamping max_payload_size instead of the shrunk SOME/IP byte count into the 8-byte SHM
// header, or crashing when GenericSkeletonEvent::Send() returns an error.
TEST_F(ProviderServiceBindingTest, EventUpdateWritesPayloadLengthAndSendsSampleToSkeletonEvent) {
    auto const binding = CreateConnectedProvider();
    OfferService();
    alignas(kSample_alignment) std::array<std::byte, kSampleBufferSize> sample_buf{};

    EXPECT_CALL(*skeleton_event_mock_, Allocate(_))
        .WillOnce([&sample_buf](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeAllocatedSample(sample_buf.data(), std::move(guard));
        });
    auto alloc_fail_send =
        client_callbacks_.on_event_payload_allocate(*client_connector_mock_, kSocomEvent0Id);
    ASSERT_TRUE(alloc_fail_send.has_value());
    EXPECT_TRUE(alloc_fail_send->shrink(kShrunkPayloadLengthOnFailure));
    EXPECT_CALL(*skeleton_event_mock_, Send(_, _))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    client_callbacks_.on_event_update(*client_connector_mock_, kSocomEvent0Id,
                                      std::move(*alloc_fail_send));

    EXPECT_CALL(*skeleton_event_mock_, Allocate(_))
        .WillOnce([&sample_buf](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeAllocatedSample(sample_buf.data(), std::move(guard));
        });
    auto alloc_ok =
        client_callbacks_.on_event_payload_allocate(*client_connector_mock_, kSocomEvent0Id);
    ASSERT_TRUE(alloc_ok.has_value());
    EXPECT_TRUE(alloc_ok->shrink(kPositivePayloadSize));
    EXPECT_CALL(*skeleton_event_mock_, Send(_, _)).WillOnce(Return(score::Result<void>{}));
    client_callbacks_.on_event_update(*client_connector_mock_, kSocomEvent0Id,
                                      std::move(*alloc_ok));
    EXPECT_EQ(read_payload_length(sample_buf.data()), kPositivePayloadSize);
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
