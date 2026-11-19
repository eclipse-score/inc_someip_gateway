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

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/gateway_ipc_binding/impl/someipd_service.hpp"
#include "score/gateway_ipc_binding/test/unit/ipc_binding_test_fixture.hpp"
#include "score/memory/data_type_size_info.h"
#include "score/mw/com/impl/com_error.h"
#include "score/mw/com/impl/configuration/quality_type.h"
#include "score/mw/com/impl/find_service_handle.h"
#include "score/mw/com/impl/find_service_handler.h"
#include "score/mw/com/impl/handle_type.h"
#include "score/mw/com/impl/instance_identifier.h"
#include "score/mw/com/impl/instance_specifier.h"
#include "score/mw/com/impl/methods/skeleton_method_binding.h"
#include "score/mw/com/impl/skeleton_binding.h"
#include "score/mw/com/impl/skeleton_event_binding.h"
#include "score/result/result.h"
#include "score/span.hpp"

namespace score::gateway_ipc_binding::mw_com {
namespace {

using ::testing::_;
using ::testing::An;
using ::testing::ByMove;
using ::testing::Ref;
using ::testing::Return;
using ::testing::TypedEq;

constexpr std::size_t kSomeipdFindHandleUid{10U};
constexpr std::size_t kBridgedFindHandleUidA{20U};
constexpr std::size_t kBridgedFindHandleUidB{21U};
constexpr std::uint16_t kFirstPeerInstanceId{1U};
constexpr std::uint16_t kSecondPeerInstanceId{2U};

constexpr std::size_t kMaxConsumerEventCapacity{Bridged_event_sizes::max_size};
constexpr std::size_t kHalfConsumerEventCapacity{kMaxConsumerEventCapacity / 2U};
constexpr std::uint16_t kSecondServiceEventIdBase{2000U};
constexpr std::size_t kBasePayloadSizeA{16U};
constexpr std::size_t kBasePayloadSizeB{2048U};
constexpr std::size_t kProviderPayloadSizeA{128U};
constexpr std::size_t kExpectedProviderSampleSizeA{136U};
constexpr std::size_t kProviderPayloadSizeB{256U};
constexpr std::size_t kExpectedProviderSampleSizeB{264U};
constexpr std::uint32_t kInjectedProviderPayloadSize{64U};
constexpr std::size_t kExpectedInjectedSampleSize{72U};
constexpr std::size_t kConsumerConfiguredPayloadSize{24U};
constexpr std::uint32_t kReconnectPayloadSize0{32U};
constexpr std::size_t kExpectedReconnectSampleSize0{40U};
constexpr std::uint32_t kReconnectPayloadSize1{48U};
constexpr std::size_t kExpectedReconnectSampleSize1{56U};
constexpr std::uint32_t kReconnectPayloadSize2{96U};
constexpr std::size_t kExpectedReconnectSampleSize2{104U};
constexpr std::uint32_t kModifiedReconnectPayloadSize{160U};
constexpr std::uint16_t kUnknownServiceId{61999U};
constexpr std::uint16_t kUnknownEventId{999U};
constexpr std::chrono::seconds kWorkerWaitTimeout{5};

constexpr std::size_t kDeterministicByteStride{131U};
constexpr std::size_t kDeterministicByteOffset{17U};
constexpr std::size_t kByteMask{0xFFU};
constexpr std::byte kFirstConfigMarkerByte{0x01U};
constexpr std::byte kSecondConfigMarkerByte{0x02U};
constexpr std::byte kConflictingPeerMarkerByte{0x99U};

constexpr std::array<std::byte, 4U> kSignBitGatewayBytes{
    std::byte{0x00U},
    std::byte{0x7FU},
    std::byte{0x80U},
    std::byte{0xFFU},
};

constexpr std::array<std::byte, 4U> kAlternateGatewayBytes{
    std::byte{0x10U},
    std::byte{0x7FU},
    std::byte{0x80U},
    std::byte{0xFEU},
};

std::vector<std::byte> MakeDeterministicConfigBytes(std::size_t const length) {
    std::vector<std::byte> bytes(length);
    for (std::size_t i = 0U; i < length; ++i) {
        if (i < kSignBitGatewayBytes.size()) {
            bytes[i] = kSignBitGatewayBytes[i];
        } else if (i + kSignBitGatewayBytes.size() >= length) {
            bytes[i] = kSignBitGatewayBytes[length - 1U - i];
        } else {
            auto const value = static_cast<std::uint8_t>(
                (i * kDeterministicByteStride + kDeterministicByteOffset) & kByteMask);
            bytes[i] = static_cast<std::byte>(value);
        }
    }
    return bytes;
}

std::vector<Event_config> MakeEventConfigs(std::size_t const count, std::uint16_t const first_id,
                                           std::size_t const base_payload_size,
                                           std::string_view const prefix = "ev_") {
    std::vector<Event_config> events{};
    events.reserve(count);
    for (std::size_t i = 0U; i < count; ++i) {
        events.push_back(Event_config{
            std::string{prefix} + std::to_string(i),
            Mw_com_event_id{static_cast<std::uint16_t>(first_id + i)},
            base_payload_size + i,
        });
    }
    return events;
}

score::mw::com::impl::InstanceSpecifier ParseSpecifier(std::string_view const specifier) {
    return score::mw::com::impl::InstanceSpecifier::Create(std::string{specifier}).value();
}

void ResetServiceConfiguration(Service_configuration& cfg) noexcept {
    cfg.gateway_config.size = 0U;
    cfg.gateway_config.data.fill(std::byte{0U});
    cfg.event_sizes.size = 0U;
    cfg.event_sizes.data.fill(Bridged_event_size{0U, 0U, 0U});
}

std::weak_ptr<int> AttachSkeletonLifetimeCanary(
    ::testing::NiceMock<score::testing::Skeleton>& skeleton) {
    auto canary = std::make_shared<int>(1);
    std::weak_ptr<int> const weak_canary{canary};
    ON_CALL(skeleton, GetBindingType())
        .WillByDefault([owned_canary = std::move(canary)]() noexcept {
            (void)owned_canary;
            return score::mw::com::impl::BindingType::kLoLa;
        });
    return weak_canary;
}

class GatewayIpcBindingMwComTest : public IpcBindingTestFixture {
   protected:
    struct ExpectedSkeletonEventSpec {
        std::string_view name;
        std::size_t expected_sample_size;
    };

    score::testing::MockSkeletonWithEvents ExpectProviderSkeletonWithEventSizes(
        std::initializer_list<ExpectedSkeletonEventSpec> const event_specs) {
        auto skel_mock = std::make_unique<::testing::NiceMock<score::testing::Skeleton>>();
        auto* const skel_ptr = skel_mock.get();
        score::testing::MockSkeletonWithEvents mocks{skel_ptr, {}};
        mocks.events.reserve(event_specs.size());

        EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
            .InSequence(skeleton_factory_sequence())
            .WillOnce(Return(ByMove(std::move(skel_mock))))
            .RetiresOnSaturation();

        for (auto const& spec : event_specs) {
            auto event_mock =
                std::make_unique<::testing::NiceMock<score::testing::GenericSkeletonEvent>>();
            auto* const event_ptr = event_mock.get();
            ON_CALL(*event_ptr, GetEventDataTypeSizeInfo())
                .WillByDefault(Return(
                    score::memory::DataTypeSizeInfo{spec.expected_sample_size, kSample_alignment}));
            mocks.events.push_back(event_ptr);
            EXPECT_CALL(generic_skeleton_event_factory_mock(),
                        Create(_, Ref(*skel_ptr), spec.name, _))
                .WillOnce([ev = std::move(event_mock), expected_size = spec.expected_sample_size](
                              score::mw::com::impl::InstanceIdentifier const&,
                              score::mw::com::impl::SkeletonBinding&, std::string_view,
                              score::memory::DataTypeSizeInfo const size_info) mutable
                              -> std::unique_ptr<score::mw::com::impl::SkeletonEventBinding> {
                    EXPECT_EQ(size_info.Size(), expected_size);
                    EXPECT_EQ(size_info.Alignment(), kSample_alignment);
                    return std::move(ev);
                })
                .RetiresOnSaturation();
        }
        return mocks;
    }

    void ExpectSomeipdServerStart(
        score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler* const out_rpc_handler) {
        StubSomeipdInstanceResolved();
        auto const skel_mocks = ExpectSkeletonWithMethod("AddServiceConfiguration");
        EXPECT_CALL(*skel_mocks.method, RegisterHandler(_))
            .WillOnce([out_rpc_handler](
                          score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler&& fn)
                          -> score::Result<void> {
                *out_rpc_handler = std::move(fn);
                return {};
            });
        EXPECT_CALL(service_discovery_mock(), OfferService(_))
            .WillOnce(Return(score::Result<void>{}));
        EXPECT_CALL(service_discovery_mock(), StopOfferService(_))
            .WillOnce(Return(score::Result<void>{}));
    }
};

TEST_F(GatewayIpcBindingMwComTest, CreateClientAcceptsEmptyGatewayConfig) {
    StubSomeipdInstanceResolved();
    auto const h1 = MakeSomeipdHandle(kSomeipdSpecifier, kFirstPeerInstanceId);
    auto const find_handle = score::mw::com::impl::make_FindServiceHandle(kSomeipdFindHandleUid);

    score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType> find_handler{};
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .WillOnce(
            [&find_handler, find_handle](
                score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType> handler,
                score::mw::com::impl::InstanceSpecifier) {
                find_handler = std::move(handler);
                return score::Result<score::mw::com::impl::FindServiceHandle>{find_handle};
            });
    EXPECT_CALL(service_discovery_mock(), StopFindService(find_handle))
        .WillOnce(Return(score::Result<void>{}));
    auto const rpc = ExpectSomeipdProxyWithRpc(score::Result<bool>{true});

    auto client = create_client(socom_runtime_, std::string{kSomeipdSpecifier}, {}, "gw_zero",
                                score::cpp::span<std::byte const>{});
    ASSERT_NE(client, nullptr);
    EXPECT_FALSE(client->is_connected());

    find_handler({h1}, find_handle);
    EXPECT_TRUE(client->is_connected());
    EXPECT_EQ(rpc.buffers.in_arg->gateway_config.size, 0U);
    EXPECT_TRUE(rpc.buffers.in_arg->gateway_config.empty());
    EXPECT_EQ(rpc.buffers.in_arg->event_sizes.size, 0U);
}

TEST_F(GatewayIpcBindingMwComTest, CreateClientAcceptsMax64KiBGatewayConfigAndPreservesBytes) {
    StubSomeipdInstanceResolved();
    auto const h1 = MakeSomeipdHandle(kSomeipdSpecifier, kFirstPeerInstanceId);
    auto const find_handle = score::mw::com::impl::make_FindServiceHandle(kSomeipdFindHandleUid);
    auto const max_bytes = MakeDeterministicConfigBytes(kMax_service_configuration_size);

    score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType> find_handler{};
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .WillOnce(
            [&find_handler, find_handle](
                score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType> handler,
                score::mw::com::impl::InstanceSpecifier) {
                find_handler = std::move(handler);
                return score::Result<score::mw::com::impl::FindServiceHandle>{find_handle};
            });
    EXPECT_CALL(service_discovery_mock(), StopFindService(find_handle))
        .WillOnce(Return(score::Result<void>{}));
    auto const rpc = ExpectSomeipdProxyWithRpc(score::Result<bool>{true});

    auto client =
        create_client(socom_runtime_, std::string{kSomeipdSpecifier}, {}, "gw_max",
                      score::cpp::span<std::byte const>{max_bytes.data(), max_bytes.size()});
    ASSERT_NE(client, nullptr);

    find_handler({h1}, find_handle);
    EXPECT_TRUE(client->is_connected());
    ASSERT_EQ(rpc.buffers.in_arg->gateway_config.size, kMax_service_configuration_size);
    std::vector<std::byte> const actual_bytes(
        rpc.buffers.in_arg->gateway_config.data.begin(),
        rpc.buffers.in_arg->gateway_config.data.begin() +
            static_cast<std::ptrdiff_t>(rpc.buffers.in_arg->gateway_config.size));
    EXPECT_EQ(actual_bytes, max_bytes);
}

TEST_F(GatewayIpcBindingMwComTest, CreateClientRejectsGatewayConfigExceedingMax64KiB) {
    StubSomeipdInstanceResolved();
    auto const overflow_bytes = MakeDeterministicConfigBytes(kMax_service_configuration_size + 1U);
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .Times(0);

    auto client = create_client(
        socom_runtime_, std::string{kSomeipdSpecifier}, {}, "gw_overflow",
        score::cpp::span<std::byte const>{overflow_bytes.data(), overflow_bytes.size()});
    EXPECT_EQ(client, nullptr);
}

TEST_F(GatewayIpcBindingMwComTest,
       CreateClientAcceptsMaxConsumerEventsAndExcludesProviderEventsFromWireSizes) {
    StubSomeipdInstanceResolved();

    auto const ids_cons_a = NextUniqueBridgedIds("cli_c512a");
    auto const ids_cons_b = NextUniqueBridgedIds("cli_c512b");
    auto const ids_prov = NextUniqueBridgedIds("cli_prov");

    auto events_a = MakeEventConfigs(kHalfConsumerEventCapacity, kEvent0Id, kBasePayloadSizeA);
    auto events_b =
        MakeEventConfigs(kHalfConsumerEventCapacity, kSecondServiceEventIdBase, kBasePayloadSizeB);
    std::vector<Event_config> const prov_events{
        Event_config{"prov_ev0", Mw_com_event_id{kEvent0Id}, kProviderPayloadSizeA},
        Event_config{"prov_ev1", Mw_com_event_id{kEvent1Id}, kProviderPayloadSizeB},
    };

    auto const svc_cons_a = MakeMultiEventServiceConfig(
        Role::consumer, events_a, ids_cons_a.specifier, ids_cons_a.service_id);
    auto const svc_cons_b = MakeMultiEventServiceConfig(
        Role::consumer, events_b, ids_cons_b.specifier, ids_cons_b.service_id);
    auto const svc_prov = MakeMultiEventServiceConfig(Role::provider, prov_events,
                                                      ids_prov.specifier, ids_prov.service_id);

    auto const spec_cons_a = ParseSpecifier(ids_cons_a.specifier);
    auto const spec_cons_b = ParseSpecifier(ids_cons_b.specifier);
    auto const spec_someipd = ParseSpecifier(kSomeipdSpecifier);

    auto const fh_cons_a = score::mw::com::impl::make_FindServiceHandle(kBridgedFindHandleUidA);
    auto const fh_cons_b = score::mw::com::impl::make_FindServiceHandle(kBridgedFindHandleUidB);
    auto const fh_someipd = score::mw::com::impl::make_FindServiceHandle(kSomeipdFindHandleUid);
    auto const h1 = MakeSomeipdHandle(kSomeipdSpecifier, kFirstPeerInstanceId);

    (void)ExpectServerConnectorCreated();
    (void)ExpectServerConnectorCreated();
    (void)ExpectProviderSkeletonWithEventSizes({
        ExpectedSkeletonEventSpec{"prov_ev0", kExpectedProviderSampleSizeA},
        ExpectedSkeletonEventSpec{"prov_ev1", kExpectedProviderSampleSizeB},
    });
    (void)ExpectClientConnectorCreated();

    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec_cons_a)))
        .WillOnce(Return(score::Result<score::mw::com::impl::FindServiceHandle>{fh_cons_a}));
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec_cons_b)))
        .WillOnce(Return(score::Result<score::mw::com::impl::FindServiceHandle>{fh_cons_b}));

    score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>
        someipd_find_handler{};
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec_someipd)))
        .WillOnce(
            [&someipd_find_handler, fh_someipd](
                score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType> handler,
                score::mw::com::impl::InstanceSpecifier) {
                someipd_find_handler = std::move(handler);
                return score::Result<score::mw::com::impl::FindServiceHandle>{fh_someipd};
            });

    EXPECT_CALL(service_discovery_mock(), StopFindService(fh_cons_a))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(service_discovery_mock(), StopFindService(fh_cons_b))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(service_discovery_mock(), StopFindService(fh_someipd))
        .WillOnce(Return(score::Result<void>{}));

    auto const rpc = ExpectSomeipdProxyWithRpc(score::Result<bool>{true});

    auto client = create_client(socom_runtime_, std::string{kSomeipdSpecifier},
                                {svc_cons_a, svc_prov, svc_cons_b}, "gw_1024",
                                score::cpp::span<std::byte const>{kSignBitGatewayBytes.data(),
                                                                  kSignBitGatewayBytes.size()});
    ASSERT_NE(client, nullptr);

    someipd_find_handler({h1}, fh_someipd);
    EXPECT_TRUE(client->is_connected());

    auto const& sent = *rpc.buffers.in_arg;
    ASSERT_EQ(sent.gateway_config.size, kSignBitGatewayBytes.size());
    for (std::size_t i = 0U; i < kSignBitGatewayBytes.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(sent.gateway_config.data[i], kSignBitGatewayBytes[i]);
    }

    ASSERT_EQ(sent.event_sizes.size, kMaxConsumerEventCapacity);
    for (std::size_t i = 0U; i < kHalfConsumerEventCapacity; ++i) {
        SCOPED_TRACE(i);
        Bridged_event_size const expected_a{
            ids_cons_a.service_id,
            static_cast<std::uint16_t>(events_a[i].id),
            static_cast<std::uint32_t>(events_a[i].max_payload_size),
        };
        EXPECT_EQ(sent.event_sizes.data[i], expected_a);
    }
    for (std::size_t i = 0U; i < kHalfConsumerEventCapacity; ++i) {
        SCOPED_TRACE(i);
        Bridged_event_size const expected_b{
            ids_cons_b.service_id,
            static_cast<std::uint16_t>(events_b[i].id),
            static_cast<std::uint32_t>(events_b[i].max_payload_size),
        };
        EXPECT_EQ(sent.event_sizes.data[kHalfConsumerEventCapacity + i], expected_b);
    }
    for (std::size_t i = 0U; i < sent.event_sizes.size; ++i) {
        SCOPED_TRACE(i);
        EXPECT_NE(sent.event_sizes.data[i].service_id, ids_prov.service_id);
    }
}

TEST_F(GatewayIpcBindingMwComTest, CreateClientRejectsConsumerEventCountExceedingMaxCapacity) {
    StubSomeipdInstanceResolved();

    auto const ids_over_a = NextUniqueBridgedIds("cli_c1025a");
    auto const ids_over_b = NextUniqueBridgedIds("cli_c1025b");
    auto const svc_over_a = MakeMultiEventServiceConfig(
        Role::consumer, MakeEventConfigs(kHalfConsumerEventCapacity, kEvent0Id, kBasePayloadSizeA),
        ids_over_a.specifier, ids_over_a.service_id);
    auto const svc_over_b =
        MakeMultiEventServiceConfig(Role::consumer,
                                    MakeEventConfigs(kHalfConsumerEventCapacity + 1U,
                                                     kSecondServiceEventIdBase, kBasePayloadSizeB),
                                    ids_over_b.specifier, ids_over_b.service_id);

    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .Times(0);

    auto client = create_client(socom_runtime_, std::string{kSomeipdSpecifier},
                                {svc_over_a, svc_over_b}, "gw_1025");
    EXPECT_EQ(client, nullptr);
}

TEST_F(GatewayIpcBindingMwComTest,
       CreateClientRejectsInvalidSecondServiceBeforeCreatingConnectorsOrStartingDiscovery) {
    StubSomeipdInstanceResolved();

    auto const ids_valid = NextUniqueBridgedIds("rb_val_ok");
    auto const valid_svc = MakeMultiEventServiceConfig(
        Role::consumer,
        {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, kEvent0MaxPayload}},
        ids_valid.specifier, ids_valid.service_id);
    auto invalid_svc = MakeServiceConfig(Role::consumer, "unbridged/service");
    invalid_svc.service_id = kSecondServiceId;

    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .Times(0);

    auto client = create_client(socom_runtime_, std::string{kSomeipdSpecifier},
                                {valid_svc, invalid_svc}, "rb_val");
    EXPECT_EQ(client, nullptr);
}

TEST_F(GatewayIpcBindingMwComTest, CreateClientRollsBackWhenSecondConnectorCreationFails) {
    StubSomeipdInstanceResolved();

    auto const ids1 = NextUniqueBridgedIds("rb_setup_1");
    auto const ids2 = NextUniqueBridgedIds("rb_setup_2");
    auto const svc1 = MakeMultiEventServiceConfig(
        Role::consumer,
        {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, kEvent0MaxPayload}},
        ids1.specifier, ids1.service_id);
    auto const svc2 = MakeMultiEventServiceConfig(
        Role::consumer,
        {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, kEvent0MaxPayload}},
        ids2.specifier, ids2.service_id);

    (void)ExpectServerConnectorCreated();
    ExpectServerConnectorCreationFailure();
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .Times(0);

    auto client =
        create_client(socom_runtime_, std::string{kSomeipdSpecifier}, {svc1, svc2}, "rb_setup");
    EXPECT_EQ(client, nullptr);
}

TEST_F(GatewayIpcBindingMwComTest,
       CreateClientStopsStartedBridgedDiscoveryWhenSecondServiceDiscoveryFails) {
    StubSomeipdInstanceResolved();
    auto const spec_someipd = ParseSpecifier(kSomeipdSpecifier);

    auto const ids1 = NextUniqueBridgedIds("rb_start_1");
    auto const ids2 = NextUniqueBridgedIds("rb_start_2");
    auto const svc1 = MakeMultiEventServiceConfig(
        Role::consumer,
        {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, kEvent0MaxPayload}},
        ids1.specifier, ids1.service_id);
    auto const svc2 = MakeMultiEventServiceConfig(
        Role::consumer,
        {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, kEvent0MaxPayload}},
        ids2.specifier, ids2.service_id);

    auto const spec1 = ParseSpecifier(ids1.specifier);
    auto const spec2 = ParseSpecifier(ids2.specifier);
    auto const fh1 = score::mw::com::impl::make_FindServiceHandle(kBridgedFindHandleUidA);

    (void)ExpectServerConnectorCreated();
    (void)ExpectServerConnectorCreated();

    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec1)))
        .WillOnce(Return(score::Result<score::mw::com::impl::FindServiceHandle>{fh1}));
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec2)))
        .WillOnce(Return(
            score::MakeUnexpected(score::mw::com::impl::ComErrc::kFindServiceHandlerFailure)));
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec_someipd)))
        .Times(0);
    EXPECT_CALL(service_discovery_mock(), StopFindService(fh1))
        .WillOnce(Return(score::Result<void>{}));

    auto client =
        create_client(socom_runtime_, std::string{kSomeipdSpecifier}, {svc1, svc2}, "rb_start");
    EXPECT_EQ(client, nullptr);
}

TEST_F(GatewayIpcBindingMwComTest,
       CreateClientStopsStartedBridgedDiscoveryWhenSomeipdDiscoveryFails) {
    StubSomeipdInstanceResolved();
    auto const spec_someipd = ParseSpecifier(kSomeipdSpecifier);

    auto const ids1 = NextUniqueBridgedIds("rb_someipd_1");
    auto const svc1 = MakeMultiEventServiceConfig(
        Role::consumer,
        {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, kEvent0MaxPayload}},
        ids1.specifier, ids1.service_id);
    auto const spec1 = ParseSpecifier(ids1.specifier);
    auto const fh1 = score::mw::com::impl::make_FindServiceHandle(kBridgedFindHandleUidB);

    (void)ExpectServerConnectorCreated();
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec1)))
        .WillOnce(Return(score::Result<score::mw::com::impl::FindServiceHandle>{fh1}));
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec_someipd)))
        .WillOnce(Return(
            score::MakeUnexpected(score::mw::com::impl::ComErrc::kFindServiceHandlerFailure)));
    EXPECT_CALL(service_discovery_mock(), StopFindService(fh1))
        .WillOnce(Return(score::Result<void>{}));

    auto client =
        create_client(socom_runtime_, std::string{kSomeipdSpecifier}, {svc1}, "rb_someipd");
    EXPECT_EQ(client, nullptr);
}

TEST_F(GatewayIpcBindingMwComTest,
       CreateClientTracksSomeipdConnectionStateAcrossDisconnectAndReconnect) {
    StubSomeipdInstanceResolved();
    auto const h1 = MakeSomeipdHandle(kSomeipdSpecifier, kFirstPeerInstanceId);
    auto const h2 = MakeSomeipdHandle(kSomeipdSpecifier, kSecondPeerInstanceId);
    auto const fh = score::mw::com::impl::make_FindServiceHandle(kSomeipdFindHandleUid);

    score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType> find_handler{};
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .WillOnce(
            [&find_handler, fh](
                score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType> handler,
                score::mw::com::impl::InstanceSpecifier) {
                find_handler = std::move(handler);
                return score::Result<score::mw::com::impl::FindServiceHandle>{fh};
            });
    EXPECT_CALL(service_discovery_mock(), StopFindService(fh))
        .WillOnce(Return(score::Result<void>{}));

    (void)ExpectSomeipdProxyWithRpc(score::Result<bool>{true});
    (void)ExpectSomeipdProxyWithRpc(score::Result<bool>{true});

    auto client = create_client(socom_runtime_, std::string{kSomeipdSpecifier});
    ASSERT_NE(client, nullptr);
    EXPECT_FALSE(client->is_connected());

    find_handler({h1}, fh);
    EXPECT_TRUE(client->is_connected());

    find_handler({}, fh);
    EXPECT_FALSE(client->is_connected());

    find_handler({h2}, fh);
    EXPECT_TRUE(client->is_connected());
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(GatewayIpcBindingMwComTest,
       DISABLED_CreateServerRejectsEmptySpecifierOrEmptyHandlerAndDefersInvalidSpecifierToStart) {
    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_)).Times(0);

    auto const null_on_empty_specifier = create_server(
        socom_runtime_, "",
        [](score::cpp::span<std::byte const>, Add_bridged_services const&) { return true; });
    EXPECT_EQ(null_on_empty_specifier, nullptr);

    auto const null_on_empty_handler =
        create_server(socom_runtime_, std::string{kSomeipdSpecifier}, Gateway_config_handler{});
    EXPECT_EQ(null_on_empty_handler, nullptr);

    auto server_with_invalid_specifier = create_server(
        socom_runtime_, std::string{kInvalidSpecifier},
        [](score::cpp::span<std::byte const>, Add_bridged_services const&) { return true; });
    ASSERT_NE(server_with_invalid_specifier, nullptr);

    auto const start_result = server_with_invalid_specifier->start();
    ASSERT_FALSE(start_result.has_value());
    EXPECT_EQ(start_result.error(),
              MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier));
}

TEST_F(GatewayIpcBindingMwComTest,
       ServerStartRetriesAfterCreationAndOfferFailuresAndRejectsDoubleStartAfterSuccess) {
    StubSomeipdInstanceResolved();
    ExpectSkeletonCreationFailure();
    (void)ExpectSkeletonWithMethod("AddServiceConfiguration");
    (void)ExpectSkeletonWithMethod("AddServiceConfiguration");
    EXPECT_CALL(service_discovery_mock(), OfferService(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(service_discovery_mock(), StopOfferService(_))
        .WillOnce(Return(score::Result<void>{}));

    auto server = create_server(
        socom_runtime_, std::string{kSomeipdSpecifier},
        [](score::cpp::span<std::byte const>, Add_bridged_services const&) { return true; });
    ASSERT_NE(server, nullptr);

    // Attempt 1: Skeleton creation fails -> returns creation error; server remains unstarted.
    auto const create_err = server->start();
    ASSERT_FALSE(create_err.has_value());
    EXPECT_EQ(create_err.error(),
              MakeError(Mw_com_binding_error::runtime_error_someipd_service_creation_failed));

    // Attempt 2: OfferService fails -> returns offer error; server remains unstarted.
    auto const offer_err = server->start();
    ASSERT_FALSE(offer_err.has_value());
    EXPECT_EQ(offer_err.error(),
              MakeError(Mw_com_binding_error::runtime_error_someipd_service_offer_failed));

    // Attempt 3: Retry start() after failed offer succeeds.
    auto const start_ok = server->start();
    ASSERT_TRUE(start_ok.has_value());

    // Attempt 4: Calling start() again after success returns logic_error_already_started.
    auto const double_start = server->start();
    ASSERT_FALSE(double_start.has_value());
    EXPECT_EQ(double_start.error(), MakeError(Mw_com_binding_error::logic_error_already_started));
}

TEST_F(GatewayIpcBindingMwComTest,
       ServerRejectsCorruptedWireSizesWithoutInvokingGatewayConfigHandler) {
    score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler rpc_handler{};
    ExpectSomeipdServerStart(&rpc_handler);

    std::size_t handler_calls{0U};
    auto server = create_server(
        socom_runtime_, std::string{kSomeipdSpecifier},
        [&handler_calls](score::cpp::span<std::byte const>, Add_bridged_services const&) {
            ++handler_calls;
            return true;
        });
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    auto call_buffers = AllocateSomeipdCallBuffers();

    // 1) Corrupted gateway_config.size > 65536 rejected before invoking handler.
    ResetServiceConfiguration(*call_buffers.in_arg);
    call_buffers.in_arg->gateway_config.size = kMax_service_configuration_size + 1U;
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_FALSE(*call_buffers.return_value);
    EXPECT_EQ(handler_calls, 0U);

    // 2) Corrupted event_sizes.size > 1024 rejected before invoking handler.
    ResetServiceConfiguration(*call_buffers.in_arg);
    call_buffers.in_arg->event_sizes.size = kMaxConsumerEventCapacity + 1U;
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_FALSE(*call_buffers.return_value);
    EXPECT_EQ(handler_calls, 0U);
}

TEST_F(GatewayIpcBindingMwComTest,
       ServerInjectsProviderEventSizeToAligned72ByteSkeletonAndRunsAsyncDiscoveryWorker) {
    auto const prov_ids = NextUniqueBridgedIds("srv_prov_64");
    auto const cons_ids_ok = NextUniqueBridgedIds("srv_cons_ok");
    auto const cons_ids_err = NextUniqueBridgedIds("srv_cons_err");

    auto const prov_cfg = MakeMultiEventServiceConfig(
        Role::provider, {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, 0U}},
        prov_ids.specifier, prov_ids.service_id);
    auto const cons_cfg_ok =
        MakeMultiEventServiceConfig(Role::consumer,
                                    {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id},
                                                  kConsumerConfiguredPayloadSize}},
                                    cons_ids_ok.specifier, cons_ids_ok.service_id);
    auto const cons_cfg_err =
        MakeMultiEventServiceConfig(Role::consumer,
                                    {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id},
                                                  kConsumerConfiguredPayloadSize}},
                                    cons_ids_err.specifier, cons_ids_err.service_id);

    auto const spec_cons_ok = ParseSpecifier(cons_ids_ok.specifier);
    auto const spec_cons_err = ParseSpecifier(cons_ids_err.specifier);
    auto const fh_cons_ok = score::mw::com::impl::make_FindServiceHandle(kBridgedFindHandleUidA);

    std::promise<std::thread::id> ok_worker_thread_promise{};
    auto ok_worker_thread_future = ok_worker_thread_promise.get_future();
    std::promise<void> err_worker_done_promise{};
    auto err_worker_done_future = err_worker_done_promise.get_future();

    score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler rpc_handler{};
    ExpectSomeipdServerStart(&rpc_handler);

    // Expect provider skeleton event created with injected 64B payload -> 72B aligned sample size.
    (void)ExpectProviderSkeletonWithEventSizes({
        ExpectedSkeletonEventSpec{kEvent0, kExpectedInjectedSampleSize},
    });
    (void)ExpectClientConnectorCreated();
    (void)ExpectServerConnectorCreated();
    (void)ExpectServerConnectorCreated();

    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec_cons_ok)))
        .WillOnce([&ok_worker_thread_promise, fh_cons_ok](
                      score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>,
                      score::mw::com::impl::InstanceSpecifier) {
            ok_worker_thread_promise.set_value(std::this_thread::get_id());
            return score::Result<score::mw::com::impl::FindServiceHandle>{fh_cons_ok};
        });
    EXPECT_CALL(
        service_discovery_mock(),
        StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec_cons_err)))
        .WillOnce([&err_worker_done_promise](
                      score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>,
                      score::mw::com::impl::InstanceSpecifier) {
            err_worker_done_promise.set_value();
            return score::Result<score::mw::com::impl::FindServiceHandle>{
                score::MakeUnexpected(score::mw::com::impl::ComErrc::kFindServiceHandlerFailure)};
        });
    EXPECT_CALL(service_discovery_mock(), StopFindService(fh_cons_ok))
        .WillOnce(Return(score::Result<void>{}));

    std::vector<std::byte> received_gateway_bytes{};
    bool add_services_ok{false};

    auto server = create_server(socom_runtime_, std::string{kSomeipdSpecifier},
                                [&received_gateway_bytes, &add_services_ok, &prov_cfg, &cons_cfg_ok,
                                 &cons_cfg_err](score::cpp::span<std::byte const> const gw_cfg,
                                                Add_bridged_services const& add_bridged_services) {
                                    received_gateway_bytes.assign(gw_cfg.begin(), gw_cfg.end());
                                    add_services_ok =
                                        add_bridged_services({prov_cfg, cons_cfg_ok, cons_cfg_err});
                                    return add_services_ok;
                                });
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    auto call_buffers = AllocateSomeipdCallBuffers();
    call_buffers.in_arg->gateway_config.size = kSignBitGatewayBytes.size();
    for (std::size_t i = 0U; i < kSignBitGatewayBytes.size(); ++i) {
        call_buffers.in_arg->gateway_config.data[i] = kSignBitGatewayBytes[i];
    }
    call_buffers.in_arg->event_sizes.size = 1U;
    call_buffers.in_arg->event_sizes.data[0U] =
        Bridged_event_size{prov_ids.service_id, kEvent0Id, kInjectedProviderPayloadSize};
    *call_buffers.return_value = false;

    auto const caller_thread_id = std::this_thread::get_id();
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);

    EXPECT_TRUE(add_services_ok);
    EXPECT_TRUE(*call_buffers.return_value);
    ASSERT_EQ(received_gateway_bytes.size(), kSignBitGatewayBytes.size());
    for (std::size_t i = 0U; i < kSignBitGatewayBytes.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(received_gateway_bytes[i], kSignBitGatewayBytes[i]);
    }

    ASSERT_EQ(ok_worker_thread_future.wait_for(kWorkerWaitTimeout), std::future_status::ready);
    EXPECT_NE(ok_worker_thread_future.get(), caller_thread_id);
    ASSERT_EQ(err_worker_done_future.wait_for(kWorkerWaitTimeout), std::future_status::ready);
}

TEST_F(GatewayIpcBindingMwComTest,
       ServerRejectsMismatchedUnknownAndDuplicateProviderEventSizesWithoutCreatingBindings) {
    // Catch-all .Times(0) placed before ExpectSomeipdServerStart (whose Create(_) retires after
    // server->start()) guarantees zero bridged skeletons or events are created on rejected RPCs.
    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_)).Times(0);
    EXPECT_CALL(generic_skeleton_event_factory_mock(), Create(_, _, _, _)).Times(0);

    score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler rpc_handler{};
    ExpectSomeipdServerStart(&rpc_handler);

    Service_configs next_services{};
    bool last_add_result{true};

    auto server = create_server(
        socom_runtime_, std::string{kSomeipdSpecifier},
        [&last_add_result, &next_services](score::cpp::span<std::byte const>,
                                           Add_bridged_services const& add_bridged_services) {
            last_add_result = add_bridged_services(next_services);
            return last_add_result;
        });
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    auto const single_prov_ids = NextUniqueBridgedIds("srv_rej_single");
    auto const single_prov_cfg = MakeMultiEventServiceConfig(
        Role::provider, {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, 0U}},
        single_prov_ids.specifier, single_prov_ids.service_id);

    auto call_buffers = AllocateSomeipdCallBuffers();

    // 1) Too few event_sizes (0 provided for 1 provider event).
    next_services = {single_prov_cfg};
    ResetServiceConfiguration(*call_buffers.in_arg);
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_FALSE(last_add_result);
    EXPECT_FALSE(*call_buffers.return_value);

    // 2) Too many event_sizes (2 provided for 1 provider event).
    ResetServiceConfiguration(*call_buffers.in_arg);
    call_buffers.in_arg->event_sizes.size = 2U;
    call_buffers.in_arg->event_sizes.data[0U] =
        Bridged_event_size{single_prov_ids.service_id, kEvent0Id, kInjectedProviderPayloadSize};
    call_buffers.in_arg->event_sizes.data[1U] =
        Bridged_event_size{single_prov_ids.service_id, kEvent1Id, kInjectedProviderPayloadSize};
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_FALSE(last_add_result);
    EXPECT_FALSE(*call_buffers.return_value);

    // 3) Unknown service_id and unknown event_id when count matches (1 == 1).
    ResetServiceConfiguration(*call_buffers.in_arg);
    call_buffers.in_arg->event_sizes.size = 1U;
    call_buffers.in_arg->event_sizes.data[0U] =
        Bridged_event_size{kUnknownServiceId, kEvent0Id, kInjectedProviderPayloadSize};
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_FALSE(last_add_result);
    EXPECT_FALSE(*call_buffers.return_value);

    call_buffers.in_arg->event_sizes.data[0U] = Bridged_event_size{
        single_prov_ids.service_id, kUnknownEventId, kInjectedProviderPayloadSize};
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_FALSE(last_add_result);
    EXPECT_FALSE(*call_buffers.return_value);

    // 4) Duplicate (service_id, event_id) where total count matches (2 provider events, ev0 sent
    //    twice and ev1 omitted).
    auto const dual_prov_ids = NextUniqueBridgedIds("srv_rej_dup");
    next_services = {MakeMultiEventServiceConfig(
        Role::provider,
        {
            Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, 0U},
            Event_config{std::string{kEvent1}, Mw_com_event_id{kEvent1Id}, 0U},
        },
        dual_prov_ids.specifier, dual_prov_ids.service_id)};
    ResetServiceConfiguration(*call_buffers.in_arg);
    call_buffers.in_arg->event_sizes.size = 2U;
    call_buffers.in_arg->event_sizes.data[0U] =
        Bridged_event_size{dual_prov_ids.service_id, kEvent0Id, kReconnectPayloadSize0};
    call_buffers.in_arg->event_sizes.data[1U] =
        Bridged_event_size{dual_prov_ids.service_id, kEvent0Id, kInjectedProviderPayloadSize};
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_FALSE(last_add_result);
    EXPECT_FALSE(*call_buffers.return_value);
}

TEST_F(GatewayIpcBindingMwComTest, ServerRollsBackBindingsAndOwnershipWhenHandlerReturnsFalse) {
    auto const prov_ids = NextUniqueBridgedIds("srv_rb_prov");
    auto const cons_ids = NextUniqueBridgedIds("srv_rb_cons");

    auto const prov_cfg = MakeMultiEventServiceConfig(
        Role::provider, {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, 0U}},
        prov_ids.specifier, prov_ids.service_id);
    auto const cons_cfg =
        MakeMultiEventServiceConfig(Role::consumer,
                                    {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id},
                                                  kConsumerConfiguredPayloadSize}},
                                    cons_ids.specifier, cons_ids.service_id);

    auto const spec_cons = ParseSpecifier(cons_ids.specifier);
    auto const fh_cons = score::mw::com::impl::make_FindServiceHandle(kBridgedFindHandleUidA);
    std::promise<void> worker_started_promise{};
    auto worker_started_future = worker_started_promise.get_future();

    score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler rpc_handler{};
    ExpectSomeipdServerStart(&rpc_handler);

    // Call 1 (rolled back because handler returns false) + Call 2 (accepted retry with same IDs).
    auto const skel_call1 = ExpectProviderSkeletonWithEventSizes({
        ExpectedSkeletonEventSpec{kEvent0, kExpectedInjectedSampleSize},
    });
    auto const weak_skel_call1 = AttachSkeletonLifetimeCanary(*skel_call1.skeleton);
    (void)ExpectClientConnectorCreated();
    (void)ExpectServerConnectorCreated();

    auto const skel_call2 = ExpectProviderSkeletonWithEventSizes({
        ExpectedSkeletonEventSpec{kEvent0, kExpectedInjectedSampleSize},
    });
    auto const weak_skel_call2 = AttachSkeletonLifetimeCanary(*skel_call2.skeleton);
    (void)ExpectClientConnectorCreated();
    (void)ExpectServerConnectorCreated();

    // StartFindService MUST run only once (for Call 2), never for rolled-back Call 1!
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec_cons)))
        .WillOnce([&worker_started_promise, fh_cons](
                      score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>,
                      score::mw::com::impl::InstanceSpecifier) {
            worker_started_promise.set_value();
            return score::Result<score::mw::com::impl::FindServiceHandle>{fh_cons};
        });
    EXPECT_CALL(service_discovery_mock(), StopFindService(fh_cons))
        .WillOnce(Return(score::Result<void>{}));

    bool handler_should_accept{false};
    bool add_ok{false};
    std::size_t handler_calls{0U};

    auto server = create_server(
        socom_runtime_, std::string{kSomeipdSpecifier},
        [&handler_calls, &add_ok, &prov_cfg, &cons_cfg, &handler_should_accept](
            score::cpp::span<std::byte const>, Add_bridged_services const& add_bridged_services) {
            ++handler_calls;
            add_ok = add_bridged_services({prov_cfg, cons_cfg});
            return handler_should_accept;
        });
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    auto call_buffers = AllocateSomeipdCallBuffers();
    call_buffers.in_arg->gateway_config.size = kSignBitGatewayBytes.size();
    for (std::size_t i = 0U; i < kSignBitGatewayBytes.size(); ++i) {
        call_buffers.in_arg->gateway_config.data[i] = kSignBitGatewayBytes[i];
    }
    call_buffers.in_arg->event_sizes.size = 1U;
    call_buffers.in_arg->event_sizes.data[0U] =
        Bridged_event_size{prov_ids.service_id, kEvent0Id, kInjectedProviderPayloadSize};

    // Call 1: add_bridged_services returns true, then handler returning false rolls back bindings
    // and does NOT record configuration or service ownership.
    handler_should_accept = false;
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);

    EXPECT_EQ(handler_calls, 1U);
    EXPECT_TRUE(add_ok);
    EXPECT_FALSE(*call_buffers.return_value);
    EXPECT_TRUE(weak_skel_call1.expired());

    // Call 2: Resending the exact same configuration and services succeeds cleanly.
    handler_should_accept = true;
    add_ok = false;
    *call_buffers.return_value = false;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);

    EXPECT_EQ(handler_calls, 2U);
    EXPECT_TRUE(add_ok);
    EXPECT_TRUE(*call_buffers.return_value);
    EXPECT_FALSE(weak_skel_call2.expired());
    ASSERT_EQ(worker_started_future.wait_for(kWorkerWaitTimeout), std::future_status::ready);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(GatewayIpcBindingMwComTest,
       DISABLED_ServerRejectsSecondAddBridgedServicesCallWithinSameHandlerInvocation) {
    auto const cons_ids = NextUniqueBridgedIds("srv_double_add");
    auto const cons_cfg =
        MakeMultiEventServiceConfig(Role::consumer,
                                    {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id},
                                                  kConsumerConfiguredPayloadSize}},
                                    cons_ids.specifier, cons_ids.service_id);
    auto const spec_cons = ParseSpecifier(cons_ids.specifier);

    score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler rpc_handler{};
    ExpectSomeipdServerStart(&rpc_handler);
    (void)ExpectServerConnectorCreated();
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec_cons)))
        .Times(0);

    Service_configs first_call_services{};
    bool first_add_ok{false};
    bool second_add_ok{true};

    auto server = create_server(
        socom_runtime_, std::string{kSomeipdSpecifier},
        [&first_add_ok, &second_add_ok, &first_call_services](
            score::cpp::span<std::byte const>, Add_bridged_services const& add_bridged_services) {
            first_add_ok = add_bridged_services(first_call_services);
            second_add_ok = add_bridged_services({});
            return first_add_ok && second_add_ok;
        });
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    auto call_buffers = AllocateSomeipdCallBuffers();

    // 1) Non-empty first add_bridged_services followed by second call in same handler -> false.
    first_call_services = {cons_cfg};
    call_buffers.in_arg->gateway_config.size = 1U;
    call_buffers.in_arg->gateway_config.data[0U] = kFirstConfigMarkerByte;
    call_buffers.in_arg->event_sizes.size = 0U;
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_TRUE(first_add_ok);
    EXPECT_FALSE(second_add_ok);
    EXPECT_FALSE(*call_buffers.return_value);

    // 2) Empty first add_bridged_services followed by second call in same handler -> false.
    first_call_services = {};
    first_add_ok = false;
    second_add_ok = true;
    call_buffers.in_arg->gateway_config.data[0U] = kSecondConfigMarkerByte;
    call_buffers.in_arg->event_sizes.size = 0U;
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_TRUE(first_add_ok);
    EXPECT_FALSE(second_add_ok);
    EXPECT_FALSE(*call_buffers.return_value);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(GatewayIpcBindingMwComTest,
       DISABLED_ServerAcceptsReconnectWithIdenticalAndPermutedEventSizesWithoutCallingHandler) {
    auto const prov_ids = NextUniqueBridgedIds("srv_perm_prov");
    Service_configs const services{MakeMultiEventServiceConfig(
        Role::provider,
        {
            Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, 0U},
            Event_config{std::string{kEvent1}, Mw_com_event_id{kEvent1Id}, 0U},
            Event_config{std::string{kEvent2}, Mw_com_event_id{kEvent2Id}, 0U},
        },
        prov_ids.specifier, prov_ids.service_id)};

    score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler rpc_handler{};
    ExpectSomeipdServerStart(&rpc_handler);

    // Exactly ONE provider skeleton and client connector are created across initial + reconnects.
    (void)ExpectProviderSkeletonWithEventSizes({
        ExpectedSkeletonEventSpec{kEvent0, kExpectedReconnectSampleSize0},
        ExpectedSkeletonEventSpec{kEvent1, kExpectedReconnectSampleSize1},
        ExpectedSkeletonEventSpec{kEvent2, kExpectedReconnectSampleSize2},
    });
    (void)ExpectClientConnectorCreated();

    std::size_t handler_calls{0U};
    bool last_add_ok{false};

    auto server = create_server(
        socom_runtime_, std::string{kSomeipdSpecifier},
        [&handler_calls, &last_add_ok, &services](
            score::cpp::span<std::byte const>, Add_bridged_services const& add_bridged_services) {
            ++handler_calls;
            last_add_ok = add_bridged_services(services);
            return last_add_ok;
        });
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    Bridged_event_size const e0{prov_ids.service_id, kEvent0Id, kReconnectPayloadSize0};
    Bridged_event_size const e1{prov_ids.service_id, kEvent1Id, kReconnectPayloadSize1};
    Bridged_event_size const e2{prov_ids.service_id, kEvent2Id, kReconnectPayloadSize2};

    auto call_buffers = AllocateSomeipdCallBuffers();
    call_buffers.in_arg->gateway_config.size = kAlternateGatewayBytes.size();
    for (std::size_t i = 0U; i < kAlternateGatewayBytes.size(); ++i) {
        call_buffers.in_arg->gateway_config.data[i] = kAlternateGatewayBytes[i];
    }
    call_buffers.in_arg->event_sizes.size = 3U;
    call_buffers.in_arg->event_sizes.data[0U] = e0;
    call_buffers.in_arg->event_sizes.data[1U] = e1;
    call_buffers.in_arg->event_sizes.data[2U] = e2;

    // 1) Initial registration [e0, e1, e2] invokes handler and creates bindings.
    *call_buffers.return_value = false;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_EQ(handler_calls, 1U);
    EXPECT_TRUE(last_add_ok);
    EXPECT_TRUE(*call_buffers.return_value);

    // 2) Reconnect with identical order [e0, e1, e2] returns true without re-invoking handler.
    *call_buffers.return_value = false;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_EQ(handler_calls, 1U);
    EXPECT_TRUE(*call_buffers.return_value);

    // 3) Reconnect with permuted event_sizes order [e2, e0, e1] returns true idempotently without
    //    re-invoking handler or duplicating bindings.
    call_buffers.in_arg->event_sizes.data[0U] = e2;
    call_buffers.in_arg->event_sizes.data[1U] = e0;
    call_buffers.in_arg->event_sizes.data[2U] = e1;
    *call_buffers.return_value = false;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_EQ(handler_calls, 1U);
    EXPECT_TRUE(*call_buffers.return_value);

    // 4) Same gateway_config with a 4-entry superset [e2, e0, e1, extra] is NOT treated as a
    //    duplicate reconnect and is dispatched to handler (which rejects the conflicting ID).
    constexpr std::size_t kSupersetEventSizeCount{4U};
    call_buffers.in_arg->event_sizes.size = kSupersetEventSizeCount;
    call_buffers.in_arg->event_sizes.data[3U] =
        Bridged_event_size{kUnknownServiceId, kUnknownEventId, kReconnectPayloadSize0};
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_EQ(handler_calls, 2U);
    EXPECT_FALSE(last_add_ok);
    EXPECT_FALSE(*call_buffers.return_value);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(GatewayIpcBindingMwComTest,
       DISABLED_ServerRollsBackPartialBindingsAndOwnershipWhenServiceBindingCreationFails) {
    auto const prov_ids = NextUniqueBridgedIds("srv_partial_prov");
    auto const cons_ids = NextUniqueBridgedIds("srv_partial_cons");

    auto const prov_cfg = MakeMultiEventServiceConfig(
        Role::provider, {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, 0U}},
        prov_ids.specifier, prov_ids.service_id);
    auto const cons_cfg =
        MakeMultiEventServiceConfig(Role::consumer,
                                    {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id},
                                                  kConsumerConfiguredPayloadSize}},
                                    cons_ids.specifier, cons_ids.service_id);

    auto const spec_cons = ParseSpecifier(cons_ids.specifier);
    auto const fh_cons = score::mw::com::impl::make_FindServiceHandle(kBridgedFindHandleUidB);
    std::promise<void> worker_started_promise{};
    auto worker_started_future = worker_started_promise.get_future();

    score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler rpc_handler{};
    ExpectSomeipdServerStart(&rpc_handler);

    // Call 1: provider binding succeeds, consumer server-connector creation fails.
    auto const skel_call1 = ExpectProviderSkeletonWithEventSizes({
        ExpectedSkeletonEventSpec{kEvent0, kExpectedInjectedSampleSize},
    });
    auto const weak_skel_call1 = AttachSkeletonLifetimeCanary(*skel_call1.skeleton);
    (void)ExpectClientConnectorCreated();
    ExpectServerConnectorCreationFailure();

    // Call 2: retry with identical IDs/specifiers succeeds once connector creation recovers.
    auto const skel_call2 = ExpectProviderSkeletonWithEventSizes({
        ExpectedSkeletonEventSpec{kEvent0, kExpectedInjectedSampleSize},
    });
    auto const weak_skel_call2 = AttachSkeletonLifetimeCanary(*skel_call2.skeleton);
    (void)ExpectClientConnectorCreated();
    (void)ExpectServerConnectorCreated();

    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(spec_cons)))
        .WillOnce([&worker_started_promise, fh_cons](
                      score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>,
                      score::mw::com::impl::InstanceSpecifier) {
            worker_started_promise.set_value();
            return score::Result<score::mw::com::impl::FindServiceHandle>{fh_cons};
        });
    EXPECT_CALL(service_discovery_mock(), StopFindService(fh_cons))
        .WillOnce(Return(score::Result<void>{}));

    bool add_ok{true};
    std::size_t handler_calls{0U};

    // Handler deliberately returns true even when add_bridged_services fails to verify that
    // Server_adapter still rejects the RPC via add_failed and rolls back ownership.
    auto server = create_server(
        socom_runtime_, std::string{kSomeipdSpecifier},
        [&handler_calls, &add_ok, &prov_cfg, &cons_cfg](
            score::cpp::span<std::byte const>, Add_bridged_services const& add_bridged_services) {
            ++handler_calls;
            add_ok = add_bridged_services({prov_cfg, cons_cfg});
            return true;
        });
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    auto call_buffers = AllocateSomeipdCallBuffers();
    call_buffers.in_arg->gateway_config.size = kSignBitGatewayBytes.size();
    for (std::size_t i = 0U; i < kSignBitGatewayBytes.size(); ++i) {
        call_buffers.in_arg->gateway_config.data[i] = kSignBitGatewayBytes[i];
    }
    call_buffers.in_arg->event_sizes.size = 1U;
    call_buffers.in_arg->event_sizes.data[0U] =
        Bridged_event_size{prov_ids.service_id, kEvent0Id, kInjectedProviderPayloadSize};

    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_EQ(handler_calls, 1U);
    EXPECT_FALSE(add_ok);
    EXPECT_FALSE(*call_buffers.return_value);
    EXPECT_TRUE(weak_skel_call1.expired());

    *call_buffers.return_value = false;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_EQ(handler_calls, 2U);
    EXPECT_TRUE(add_ok);
    EXPECT_TRUE(*call_buffers.return_value);
    EXPECT_FALSE(weak_skel_call2.expired());
    ASSERT_EQ(worker_started_future.wait_for(kWorkerWaitTimeout), std::future_status::ready);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(GatewayIpcBindingMwComTest,
       DISABLED_ServerRejectsDifferentConfigurationReusingOwnedServiceIdOrInstanceSpecifier) {
    auto const owned_ids = NextUniqueBridgedIds("srv_owned_base");
    auto const alt_spec_only = NextUniqueBridgedIds("srv_alt_spec");
    auto const alt_id_only = NextUniqueBridgedIds("srv_alt_id");

    StubInstanceResolved(owned_ids.specifier, {kEvent0}, owned_ids.service_id, kDefaultInstanceId);
    StubInstanceResolved(alt_spec_only.specifier, {kEvent0}, owned_ids.service_id,
                         kDefaultInstanceId);

    score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler rpc_handler{};
    ExpectSomeipdServerStart(&rpc_handler);

    // Only the initial peer's skeleton and connector may be created.
    (void)ExpectProviderSkeletonWithEventSizes({
        ExpectedSkeletonEventSpec{kEvent0, kExpectedReconnectSampleSize0},
    });
    (void)ExpectClientConnectorCreated();

    Service_configs next_services{MakeMultiEventServiceConfig(
        Role::provider, {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, 0U}},
        owned_ids.specifier, owned_ids.service_id)};

    std::size_t handler_calls{0U};
    bool last_add_ok{false};

    auto server = create_server(
        socom_runtime_, std::string{kSomeipdSpecifier},
        [&handler_calls, &last_add_ok, &next_services](
            score::cpp::span<std::byte const>, Add_bridged_services const& add_bridged_services) {
            ++handler_calls;
            last_add_ok = add_bridged_services(next_services);
            return last_add_ok;
        });
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    auto call_buffers = AllocateSomeipdCallBuffers();
    call_buffers.in_arg->gateway_config.size = kAlternateGatewayBytes.size();
    for (std::size_t i = 0U; i < kAlternateGatewayBytes.size(); ++i) {
        call_buffers.in_arg->gateway_config.data[i] = kAlternateGatewayBytes[i];
    }
    call_buffers.in_arg->event_sizes.size = 1U;
    call_buffers.in_arg->event_sizes.data[0U] =
        Bridged_event_size{owned_ids.service_id, kEvent0Id, kReconnectPayloadSize0};

    // 1) Initial configuration claims owned_ids.service_id and owned_ids.specifier.
    *call_buffers.return_value = false;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    ASSERT_EQ(handler_calls, 1U);
    ASSERT_TRUE(last_add_ok);
    ASSERT_TRUE(*call_buffers.return_value);

    // 2) Modified max_payload_size for owned_ids.service_id (even on a fresh specifier) is NOT a
    //    reconnect and is rejected due to service_id ownership conflict before skeleton creation.
    next_services = {MakeMultiEventServiceConfig(
        Role::provider, {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, 0U}},
        alt_spec_only.specifier, owned_ids.service_id)};
    call_buffers.in_arg->event_sizes.data[0U] =
        Bridged_event_size{owned_ids.service_id, kEvent0Id, kModifiedReconnectPayloadSize};
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_EQ(handler_calls, 2U);
    EXPECT_FALSE(last_add_ok);
    EXPECT_FALSE(*call_buffers.return_value);

    // 3) Different peer reusing owned_ids.specifier with a fresh service_id is rejected due to
    //    instance_specifier ownership conflict before skeleton creation.
    next_services = {MakeMultiEventServiceConfig(
        Role::provider, {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, 0U}},
        owned_ids.specifier, alt_id_only.service_id)};
    call_buffers.in_arg->gateway_config.data[0U] = kConflictingPeerMarkerByte;
    call_buffers.in_arg->event_sizes.data[0U] =
        Bridged_event_size{alt_id_only.service_id, kEvent0Id, kReconnectPayloadSize0};
    *call_buffers.return_value = true;
    rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                call_buffers.return_bytes);
    EXPECT_EQ(handler_calls, 3U);
    EXPECT_FALSE(last_add_ok);
    EXPECT_FALSE(*call_buffers.return_value);
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
