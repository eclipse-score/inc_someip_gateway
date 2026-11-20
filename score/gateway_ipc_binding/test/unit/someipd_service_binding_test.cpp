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

#include "score/gateway_ipc_binding/impl/someipd_service_binding.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
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

#include "score/gateway_ipc_binding/error.hpp"
#include "score/gateway_ipc_binding/test/unit/ipc_binding_test_fixture.hpp"
#include "score/mw/com/impl/com_error.h"
#include "score/mw/com/impl/configuration/quality_type.h"
#include "score/mw/com/impl/find_service_handle.h"
#include "score/mw/com/impl/find_service_handler.h"
#include "score/mw/com/impl/handle_type.h"
#include "score/mw/com/impl/instance_specifier.h"
#include "score/mw/com/impl/methods/skeleton_method_binding.h"
#include "score/result/result.h"

namespace score::gateway_ipc_binding::mw_com {
namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Return;
using ::testing::TypedEq;

constexpr std::size_t kFindHandleUid{7U};
constexpr std::uint16_t kFirstPeerInstanceId{1U};
constexpr std::uint16_t kSecondPeerInstanceId{2U};
constexpr std::uint16_t kSampleServiceIdA{0x1234U};
constexpr std::uint16_t kSampleServiceIdB{0x8001U};
constexpr std::uint16_t kSampleEventIdA{0x0001U};
constexpr std::uint16_t kSampleEventIdB{0xFFFFU};
constexpr std::uint32_t kSamplePayloadSizeA{64U};
constexpr std::uint32_t kSamplePayloadSizeB{4096U};
constexpr std::size_t kExpectedMethodNameIndex{0U};
constexpr std::chrono::milliseconds kReentrantCallbackTimeout{1500};
constexpr std::byte kMutatedGatewayConfigByte{0x81U};

constexpr std::array<std::byte, 5U> kGatewayConfigBytes{
    std::byte{0x00U}, std::byte{0x7FU}, std::byte{0x80U}, std::byte{0xFFU}, std::byte{0x5AU},
};

template <typename C, typename = void>
struct HasInequalityOperator : std::false_type {};

template <typename C>
struct HasInequalityOperator<
    C, std::void_t<decltype(std::declval<C const&>() != std::declval<C const&>())>>
    : std::true_type {};

template <typename C>
void ExpectNeWhenAvailable(C const& lhs, C const& rhs, bool const expected) {
    if constexpr (HasInequalityOperator<C>::value) {
        EXPECT_EQ(lhs != rhs, expected);
    }
}

template <typename C>
void ExpectEqNoexcept(C const& lhs, C const& rhs) {
    EXPECT_TRUE(noexcept(lhs == rhs));
    if constexpr (HasInequalityOperator<C>::value) {
        EXPECT_TRUE(noexcept(lhs != rhs));
    }
}

void PopulateConfiguration(Service_configuration& cfg) noexcept {
    cfg.gateway_config.size = kGatewayConfigBytes.size();
    for (std::size_t i = 0U; i < kGatewayConfigBytes.size(); ++i) {
        cfg.gateway_config.data[i] = kGatewayConfigBytes[i];
    }
    cfg.event_sizes.size = 2U;
    cfg.event_sizes.data[0U] =
        Bridged_event_size{kSampleServiceIdA, kSampleEventIdA, kSamplePayloadSizeA};
    cfg.event_sizes.data[1U] =
        Bridged_event_size{kSampleServiceIdB, kSampleEventIdB, kSamplePayloadSizeB};
}

std::unique_ptr<Service_configuration> MakePopulatedConfiguration() {
    auto cfg = std::make_unique<Service_configuration>();
    PopulateConfiguration(*cfg);
    return cfg;
}

std::unique_ptr<std::array<Service_configuration, 2U>> MakePaddedConfigurationArena() {
    auto arena = std::make_unique<std::array<Service_configuration, 2U>>();
    PopulateConfiguration((*arena)[0U]);
    return arena;
}

std::weak_ptr<int> AttachProxyLifetimeCanary(::testing::NiceMock<score::testing::Proxy>& proxy) {
    auto canary = std::make_shared<int>(1);
    std::weak_ptr<int> const weak_canary{canary};
    ON_CALL(proxy, IsEventProvided(_))
        .WillByDefault([owned_canary = std::move(canary)](std::string_view) {
            (void)owned_canary;
            return false;
        });
    return weak_canary;
}

using SomeipdServiceBindingTest = IpcBindingTestFixture;

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(SomeipdServiceBindingTest,
       DISABLED_WireTypesEnforceFieldEqualityInequalityAndCorruptedSizeBounds) {
    static_assert(std::is_trivially_copyable_v<Bridged_event_size>);
    static_assert(std::is_trivially_copyable_v<Service_configuration>);

    Bridged_event_size const base{kSampleServiceIdA, kSampleEventIdA, kSamplePayloadSizeA};
    Bridged_event_size const same{kSampleServiceIdA, kSampleEventIdA, kSamplePayloadSizeA};
    Bridged_event_size const diff_service{kSampleServiceIdB, kSampleEventIdA, kSamplePayloadSizeA};
    Bridged_event_size const diff_event{kSampleServiceIdA, kSampleEventIdB, kSamplePayloadSizeA};
    Bridged_event_size const diff_payload{kSampleServiceIdA, kSampleEventIdA, kSamplePayloadSizeB};

    EXPECT_TRUE((HasInequalityOperator<Bridged_event_size>::value));
    ExpectEqNoexcept(base, same);
    EXPECT_TRUE(base == same);
    ExpectNeWhenAvailable(base, same, false);
    EXPECT_FALSE(base == diff_service);
    ExpectNeWhenAvailable(base, diff_service, true);
    EXPECT_FALSE(base == diff_event);
    ExpectNeWhenAvailable(base, diff_event, true);
    EXPECT_FALSE(base == diff_payload);
    ExpectNeWhenAvailable(base, diff_payload, true);

    auto const lhs_arena = MakePaddedConfigurationArena();
    auto const rhs_arena = MakePaddedConfigurationArena();
    auto& cfg_lhs = (*lhs_arena)[0U];
    auto& cfg_rhs = (*rhs_arena)[0U];
    EXPECT_TRUE((HasInequalityOperator<Service_configuration>::value));
    ExpectEqNoexcept(cfg_lhs, cfg_rhs);
    EXPECT_TRUE(cfg_lhs == cfg_rhs);
    ExpectNeWhenAvailable(cfg_lhs, cfg_rhs, false);

    cfg_rhs.gateway_config.data[2U] = kMutatedGatewayConfigByte;
    EXPECT_FALSE(cfg_lhs == cfg_rhs);
    ExpectNeWhenAvailable(cfg_lhs, cfg_rhs, true);
    cfg_rhs.gateway_config.data[2U] = kGatewayConfigBytes[2U];

    cfg_rhs.event_sizes.data[1U].max_payload_size = kSamplePayloadSizeA;
    EXPECT_FALSE(cfg_lhs == cfg_rhs);
    ExpectNeWhenAvailable(cfg_lhs, cfg_rhs, true);
    cfg_rhs.event_sizes.data[1U].max_payload_size = kSamplePayloadSizeB;
    ASSERT_TRUE(cfg_lhs == cfg_rhs);

    cfg_lhs.gateway_config.size = kMax_service_configuration_size + 1U;
    cfg_rhs.gateway_config.size = kMax_service_configuration_size + 1U;
    EXPECT_FALSE(cfg_lhs == cfg_rhs);
    ExpectNeWhenAvailable(cfg_lhs, cfg_rhs, true);
    cfg_lhs.gateway_config.size = kGatewayConfigBytes.size();
    cfg_rhs.gateway_config.size = kGatewayConfigBytes.size();

    cfg_lhs.event_sizes.size = Bridged_event_sizes::max_size + 1U;
    cfg_rhs.event_sizes.size = Bridged_event_sizes::max_size + 1U;
    EXPECT_FALSE(cfg_lhs == cfg_rhs);
    ExpectNeWhenAvailable(cfg_lhs, cfg_rhs, true);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(SomeipdServiceBindingTest,
       DISABLED_ProviderCreateRejectsEmptyHandlerBeforeTouchingSkeletonFactory) {
    StubSomeipdInstanceResolved();
    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_)).Times(0);
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);

    auto result = Someipd_service_provider::create(
        std::string{kSomeipdSpecifier},
        Someipd_service_provider::Add_service_configuration_handler{});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(),
              MakeError(Mw_com_binding_error::logic_error_invalid_service_configuration));
}

TEST_F(SomeipdServiceBindingTest, ProviderCreateRejectsInvalidInstanceSpecifiers) {
    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_)).Times(0);
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);

    auto const empty_result = Someipd_service_provider::create(
        "", [](Service_configuration const&) noexcept { return true; });
    ASSERT_FALSE(empty_result.has_value());
    EXPECT_EQ(empty_result.error(),
              MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier));

    auto const malformed_result = Someipd_service_provider::create(
        std::string{kInvalidSpecifier}, [](Service_configuration const&) noexcept { return true; });
    ASSERT_FALSE(malformed_result.has_value());
    EXPECT_EQ(malformed_result.error(),
              MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier));
}

TEST_F(SomeipdServiceBindingTest, ProviderCreateReturnsCreationFailedWhenInstanceIsUndeployed) {
    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_)).Times(0);
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);

    auto const undeployed_result = Someipd_service_provider::create(
        std::string{kSomeipdSpecifier}, [](Service_configuration const&) noexcept { return true; });
    ASSERT_FALSE(undeployed_result.has_value());
    EXPECT_EQ(undeployed_result.error(),
              MakeError(Mw_com_binding_error::runtime_error_someipd_service_creation_failed));
}

TEST_F(SomeipdServiceBindingTest, ProviderCreateReturnsCreationFailedWhenSkeletonFactoryFails) {
    StubSomeipdInstanceResolved();
    ExpectSkeletonCreationFailure();
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);

    auto const factory_failure_result = Someipd_service_provider::create(
        std::string{kSomeipdSpecifier}, [](Service_configuration const&) noexcept { return true; });
    ASSERT_FALSE(factory_failure_result.has_value());
    EXPECT_EQ(factory_failure_result.error(),
              MakeError(Mw_com_binding_error::runtime_error_someipd_service_creation_failed));
}

TEST_F(SomeipdServiceBindingTest, ProviderCreateReturnsCreationFailedWhenRegisterHandlerFails) {
    StubSomeipdInstanceResolved();
    auto const skel_mocks = ExpectSkeletonWithMethod("AddServiceConfiguration");
    EXPECT_CALL(*skel_mocks.method, RegisterHandler(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);
    EXPECT_CALL(service_discovery_mock(), StopOfferService(_)).Times(0);
    EXPECT_CALL(service_discovery_mock(), StopOfferService(_, _)).Times(0);

    auto const result = Someipd_service_provider::create(
        std::string{kSomeipdSpecifier}, [](Service_configuration const&) noexcept { return true; });

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(),
              MakeError(Mw_com_binding_error::runtime_error_someipd_service_creation_failed));
}

TEST_F(SomeipdServiceBindingTest, ProviderDestructorDoesNotStopOfferWhenNeverOffered) {
    StubSomeipdInstanceResolved();
    auto const skel_mocks = ExpectSkeletonWithMethod("AddServiceConfiguration");
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);
    EXPECT_CALL(service_discovery_mock(), StopOfferService(_)).Times(0);
    EXPECT_CALL(service_discovery_mock(), StopOfferService(_, _)).Times(0);
    EXPECT_CALL(*skel_mocks.skeleton, PrepareStopOffer(_)).Times(0);

    auto result = Someipd_service_provider::create(
        std::string{kSomeipdSpecifier}, [](Service_configuration const&) noexcept { return true; });
    ASSERT_TRUE(result.has_value());
    result->reset();
}

TEST_F(SomeipdServiceBindingTest,
       ProviderOfferReturnsOfferFailedAndSkipsStopOfferWhenPrepareOfferFails) {
    StubSomeipdInstanceResolved();
    auto const skel_mocks = ExpectSkeletonWithMethod("AddServiceConfiguration");
    EXPECT_CALL(*skel_mocks.skeleton, PrepareOffer(_, _, _))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);
    EXPECT_CALL(service_discovery_mock(), StopOfferService(_)).Times(0);
    EXPECT_CALL(service_discovery_mock(), StopOfferService(_, _)).Times(0);
    EXPECT_CALL(*skel_mocks.skeleton, PrepareStopOffer(_)).Times(0);

    auto provider = Someipd_service_provider::create(
        std::string{kSomeipdSpecifier}, [](Service_configuration const&) noexcept { return true; });
    ASSERT_TRUE(provider.has_value());

    auto const offer_result = (*provider)->offer();
    ASSERT_FALSE(offer_result.has_value());
    EXPECT_EQ(offer_result.error(),
              MakeError(Mw_com_binding_error::runtime_error_someipd_service_offer_failed));
}

TEST_F(SomeipdServiceBindingTest,
       ProviderOfferReturnsOfferFailedAndSkipsStopOfferWhenServiceDiscoveryFails) {
    StubSomeipdInstanceResolved();
    (void)ExpectSkeletonWithMethod("AddServiceConfiguration");
    EXPECT_CALL(service_discovery_mock(), OfferService(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    EXPECT_CALL(service_discovery_mock(), StopOfferService(_)).Times(0);
    EXPECT_CALL(service_discovery_mock(), StopOfferService(_, _)).Times(0);

    auto provider = Someipd_service_provider::create(
        std::string{kSomeipdSpecifier}, [](Service_configuration const&) noexcept { return true; });
    ASSERT_TRUE(provider.has_value());

    auto const offer_result = (*provider)->offer();
    ASSERT_FALSE(offer_result.has_value());
    EXPECT_EQ(offer_result.error(),
              MakeError(Mw_com_binding_error::runtime_error_someipd_service_offer_failed));
}

TEST_F(SomeipdServiceBindingTest, ProviderOffersWithdrawsOnDestructionAndForwardsRpcResponses) {
    StubSomeipdInstanceResolved();
    auto const skel_mocks = ExpectSkeletonWithMethod("AddServiceConfiguration");

    score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler registered_rpc_handler{};
    EXPECT_CALL(*skel_mocks.method, RegisterHandler(_))
        .WillOnce([&registered_rpc_handler](
                      score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler&& handler)
                      -> score::Result<void> {
            registered_rpc_handler = std::move(handler);
            return {};
        });
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(service_discovery_mock(), StopOfferService(_))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*skel_mocks.skeleton, PrepareStopOffer(_)).Times(1);

    bool next_handler_reply{true};
    auto received_cfg = std::make_unique<Service_configuration>();
    std::size_t handler_invocations{0U};

    auto provider = Someipd_service_provider::create(
        std::string{kSomeipdSpecifier}, [&received_cfg, &handler_invocations, &next_handler_reply](
                                            Service_configuration const& cfg) noexcept {
            *received_cfg = cfg;
            ++handler_invocations;
            return next_handler_reply;
        });
    ASSERT_TRUE(provider.has_value());
    ASSERT_TRUE((*provider)->offer().has_value());

    auto const expected_cfg = MakePopulatedConfiguration();
    auto call_buffers = AllocateSomeipdCallBuffers();
    *call_buffers.in_arg = *expected_cfg;

    *call_buffers.return_value = false;
    next_handler_reply = true;
    registered_rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                           call_buffers.return_bytes);
    EXPECT_EQ(handler_invocations, 1U);
    EXPECT_TRUE(*call_buffers.return_value);
    ASSERT_EQ(received_cfg->gateway_config.size, kGatewayConfigBytes.size());
    for (std::size_t i = 0U; i < kGatewayConfigBytes.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(received_cfg->gateway_config.data[i], kGatewayConfigBytes[i]);
    }
    ASSERT_EQ(received_cfg->event_sizes.size, 2U);
    EXPECT_EQ(received_cfg->event_sizes.data[0U], expected_cfg->event_sizes.data[0U]);
    EXPECT_EQ(received_cfg->event_sizes.data[1U], expected_cfg->event_sizes.data[1U]);

    *call_buffers.return_value = true;
    next_handler_reply = false;
    registered_rpc_handler(score::mw::com::impl::QualityType::kASIL_QM, call_buffers.in_arg_bytes,
                           call_buffers.return_bytes);
    EXPECT_EQ(handler_invocations, 2U);
    EXPECT_FALSE(*call_buffers.return_value);
}

TEST_F(SomeipdServiceBindingTest, ConsumerCreateRejectsInvalidAndUndeployedSpecifiers) {
    auto const cfg = MakePopulatedConfiguration();
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .Times(0);

    auto const empty_result = Someipd_service_consumer::create("", *cfg);
    ASSERT_FALSE(empty_result.has_value());
    EXPECT_EQ(empty_result.error(),
              MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier));

    auto const malformed_result =
        Someipd_service_consumer::create(std::string{kInvalidSpecifier}, *cfg);
    ASSERT_FALSE(malformed_result.has_value());
    EXPECT_EQ(malformed_result.error(),
              MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier));

    auto const undeployed_result =
        Someipd_service_consumer::create(std::string{kSomeipdSpecifier}, *cfg);
    ASSERT_FALSE(undeployed_result.has_value());
    EXPECT_EQ(undeployed_result.error(),
              MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier));
}

TEST_F(SomeipdServiceBindingTest, ConsumerCreateReturnsFindFailedWhenStartFindServiceFails) {
    StubSomeipdInstanceResolved();
    auto const cfg = MakePopulatedConfiguration();
    auto const expected_specifier =
        score::mw::com::impl::InstanceSpecifier::Create(std::string{kSomeipdSpecifier}).value();

    EXPECT_CALL(
        service_discovery_mock(),
        StartFindService(_, TypedEq<score::mw::com::impl::InstanceSpecifier>(expected_specifier)))
        .WillOnce(Return(
            score::MakeUnexpected(score::mw::com::impl::ComErrc::kFindServiceHandlerFailure)));
    EXPECT_CALL(service_discovery_mock(), StopFindService(_)).Times(0);

    auto const result = Someipd_service_consumer::create(std::string{kSomeipdSpecifier}, *cfg);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(),
              MakeError(Mw_com_binding_error::runtime_error_someipd_service_find_failed));
}

TEST_F(SomeipdServiceBindingTest, ConsumerConnectsSynchronouslyWhenDiscoveryFiresInsideCreate) {
    StubSomeipdInstanceResolved();
    auto const cfg = MakePopulatedConfiguration();
    auto const handle = MakeSomeipdHandle(kSomeipdSpecifier, kFirstPeerInstanceId);
    auto const find_handle = score::mw::com::impl::make_FindServiceHandle(kFindHandleUid);
    auto const rpc = ExpectSomeipdProxyWithRpc(score::Result<bool>{true});

    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .WillOnce(
            [&handle, find_handle](
                score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType> handler,
                score::mw::com::impl::InstanceSpecifier) {
                handler({handle}, find_handle);
                return score::Result<score::mw::com::impl::FindServiceHandle>{find_handle};
            });
    EXPECT_CALL(service_discovery_mock(), StopFindService(find_handle))
        .WillOnce(Return(score::Result<void>{}));

    auto consumer = Someipd_service_consumer::create(std::string{kSomeipdSpecifier}, *cfg);
    ASSERT_TRUE(consumer.has_value());
    EXPECT_TRUE((*consumer)->is_connected());
    EXPECT_TRUE(*rpc.buffers.in_arg == *cfg);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(SomeipdServiceBindingTest,
       DISABLED_ConsumerTracksDiscoveryTransitionsIgnoresDuplicateActiveHandleAndReconnects) {
    StubSomeipdInstanceResolved();
    auto const cfg = MakePopulatedConfiguration();
    auto const h1 = MakeSomeipdHandle(kSomeipdSpecifier, kFirstPeerInstanceId);
    auto const h2 = MakeSomeipdHandle(kSomeipdSpecifier, kSecondPeerInstanceId);
    EXPECT_FALSE(h1 == h2);

    auto const find_handle = score::mw::com::impl::make_FindServiceHandle(kFindHandleUid);
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

    auto const rpc1 = ExpectSomeipdProxyWithRpc(score::Result<bool>{true});
    auto const weak_proxy1 = AttachProxyLifetimeCanary(*rpc1.proxy);
    auto const rpc2 = ExpectSomeipdProxyWithRpc(score::Result<bool>{true});
    auto const weak_proxy2 = AttachProxyLifetimeCanary(*rpc2.proxy);
    auto const rpc3 = ExpectSomeipdProxyWithRpc(score::Result<bool>{true});
    auto const weak_proxy3 = AttachProxyLifetimeCanary(*rpc3.proxy);

    auto consumer = Someipd_service_consumer::create(std::string{kSomeipdSpecifier}, *cfg);
    ASSERT_TRUE(consumer.has_value());
    EXPECT_FALSE((*consumer)->is_connected());

    // Empty discovery while disconnected stays false without creating any proxy.
    find_handler({}, find_handle);
    EXPECT_FALSE((*consumer)->is_connected());

    // First handle connects and sends exact Service_configuration.
    find_handler({h1}, find_handle);
    EXPECT_TRUE((*consumer)->is_connected());
    EXPECT_FALSE(weak_proxy1.expired());
    ASSERT_EQ(rpc1.buffers.in_arg->gateway_config.size, kGatewayConfigBytes.size());
    for (std::size_t i = 0U; i < kGatewayConfigBytes.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(rpc1.buffers.in_arg->gateway_config.data[i], kGatewayConfigBytes[i]);
    }
    ASSERT_EQ(rpc1.buffers.in_arg->event_sizes.size, 2U);
    EXPECT_EQ(rpc1.buffers.in_arg->event_sizes.data[0U], cfg->event_sizes.data[0U]);
    EXPECT_EQ(rpc1.buffers.in_arg->event_sizes.data[1U], cfg->event_sizes.data[1U]);

    // Duplicate notification for already-connected h1 keeps proxy1 alive without re-running RPC.
    find_handler({h1}, find_handle);
    EXPECT_TRUE((*consumer)->is_connected());
    EXPECT_FALSE(weak_proxy1.expired());

    // Distinct handle h2 directly replaces proxy1 and connects proxy2.
    find_handler({h2}, find_handle);
    EXPECT_TRUE((*consumer)->is_connected());
    EXPECT_TRUE(weak_proxy1.expired());
    EXPECT_FALSE(weak_proxy2.expired());
    EXPECT_TRUE(*rpc2.buffers.in_arg == *cfg);

    // Empty discovery drops connection and destroys active proxy2.
    find_handler({}, find_handle);
    EXPECT_FALSE((*consumer)->is_connected());
    EXPECT_TRUE(weak_proxy2.expired());

    // Subsequent discovery reconnects cleanly.
    find_handler({h1}, find_handle);
    EXPECT_TRUE((*consumer)->is_connected());
    EXPECT_FALSE(weak_proxy3.expired());
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(SomeipdServiceBindingTest,
       DISABLED_ConsumerDoesNotRetainProxyWhenCreateOrRpcFailsAndRetriesSameHandle) {
    StubSomeipdInstanceResolved();
    auto const cfg = MakePopulatedConfiguration();
    auto const h1 = MakeSomeipdHandle(kSomeipdSpecifier, kFirstPeerInstanceId);
    auto const find_handle = score::mw::com::impl::make_FindServiceHandle(kFindHandleUid);
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
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));

    // Attempt 1: Proxy creation fails -> stays disconnected.
    ExpectProxyCreationFailure();

    // Attempt 2: RPC transport error -> stays disconnected and destroys rejected proxy immediately.
    auto const rpc_transport_err = ExpectSomeipdProxyWithRpc(
        score::MakeUnexpected<bool>(score::mw::com::impl::ComErrc::kBindingFailure));
    auto const weak_transport_err_proxy = AttachProxyLifetimeCanary(*rpc_transport_err.proxy);

    // Attempt 3: Peer rejects configuration (returns false) -> stays disconnected and destroys
    // proxy.
    auto const rpc_rejected = ExpectSomeipdProxyWithRpc(score::Result<bool>{false});
    auto const weak_rejected_proxy = AttachProxyLifetimeCanary(*rpc_rejected.proxy);

    // Attempt 4: Retry with the exact same handle h1 succeeds and retains active proxy until
    // teardown.
    auto const rpc_accepted = ExpectSomeipdProxyWithRpc(score::Result<bool>{true});
    auto const weak_accepted_proxy = AttachProxyLifetimeCanary(*rpc_accepted.proxy);

    auto consumer = Someipd_service_consumer::create(std::string{kSomeipdSpecifier}, *cfg);
    ASSERT_TRUE(consumer.has_value());

    find_handler({h1}, find_handle);
    EXPECT_FALSE((*consumer)->is_connected());

    find_handler({h1}, find_handle);
    EXPECT_FALSE((*consumer)->is_connected());
    EXPECT_TRUE(weak_transport_err_proxy.expired());

    find_handler({h1}, find_handle);
    EXPECT_FALSE((*consumer)->is_connected());
    EXPECT_TRUE(weak_rejected_proxy.expired());

    find_handler({h1}, find_handle);
    EXPECT_TRUE((*consumer)->is_connected());
    EXPECT_FALSE(weak_accepted_proxy.expired());

    // Destructor cleans up active proxy safely even when StopFindService returns an error.
    consumer->reset();
    EXPECT_TRUE(weak_accepted_proxy.expired());
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(SomeipdServiceBindingTest,
       DISABLED_ConsumerReleasesInternalLocksAcrossProxyCreateAndAddServiceConfigurationRpc) {
    StubSomeipdInstanceResolved();
    auto const cfg = MakePopulatedConfiguration();
    auto const h1 = MakeSomeipdHandle(kSomeipdSpecifier, kFirstPeerInstanceId);
    auto const h2 = MakeSomeipdHandle(kSomeipdSpecifier, kSecondPeerInstanceId);
    auto const find_handle = score::mw::com::impl::make_FindServiceHandle(kFindHandleUid);
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

    // 1) Re-entrant discovery callback during ProxyBindingFactory::Create must not deadlock.
    std::thread deferred_create_worker{};
    EXPECT_CALL(proxy_binding_factory_mock(), Create(_))
        .InSequence(proxy_factory_sequence())
        .WillOnce([&find_handler, find_handle,
                   &deferred_create_worker](score::mw::com::impl::HandleType const&) {
            auto done_promise = std::make_shared<std::promise<void>>();
            auto done_future = done_promise->get_future();
            deferred_create_worker = std::thread([&find_handler, find_handle, done_promise]() {
                find_handler({}, find_handle);
                done_promise->set_value();
            });
            auto const status = done_future.wait_for(kReentrantCallbackTimeout);
            EXPECT_EQ(status, std::future_status::ready);
            if (status == std::future_status::ready) {
                deferred_create_worker.join();
            }
            return std::unique_ptr<score::mw::com::impl::ProxyBinding>{};
        })
        .RetiresOnSaturation();

    // 2) Connect h1 cleanly so consumer holds an active connection before testing RPC lock scope.
    auto const rpc_initial = ExpectSomeipdProxyWithRpc(score::Result<bool>{true});
    (void)rpc_initial;

    // 3) Re-entrant discovery callback during AddServiceConfiguration DoCall must not deadlock.
    auto const rpc_reentrant = ExpectSomeipdProxyWithRpc(std::nullopt);
    std::thread deferred_rpc_worker{};
    EXPECT_CALL(*rpc_reentrant.method, DoCall(kExpectedMethodNameIndex))
        .WillOnce([&find_handler, find_handle, &deferred_rpc_worker](std::size_t) {
            auto done_promise = std::make_shared<std::promise<void>>();
            auto done_future = done_promise->get_future();
            deferred_rpc_worker = std::thread([&find_handler, find_handle, done_promise]() {
                find_handler({}, find_handle);
                done_promise->set_value();
            });
            auto const status = done_future.wait_for(kReentrantCallbackTimeout);
            EXPECT_EQ(status, std::future_status::ready);
            if (status == std::future_status::ready) {
                deferred_rpc_worker.join();
            }
            return score::MakeUnexpected<void>(score::mw::com::impl::ComErrc::kBindingFailure);
        })
        .RetiresOnSaturation();

    auto consumer = Someipd_service_consumer::create(std::string{kSomeipdSpecifier}, *cfg);
    ASSERT_TRUE(consumer.has_value());

    find_handler({h1}, find_handle);
    if (deferred_create_worker.joinable()) {
        deferred_create_worker.join();
    }
    EXPECT_FALSE((*consumer)->is_connected());

    find_handler({h1}, find_handle);
    EXPECT_TRUE((*consumer)->is_connected());

    find_handler({h2}, find_handle);
    if (deferred_rpc_worker.joinable()) {
        deferred_rpc_worker.join();
    }
    EXPECT_FALSE((*consumer)->is_connected());
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
