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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <score/span.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/gateway_ipc_binding/impl/someipd_service.hpp"
#include "score/gateway_ipc_binding/test/unit/ipc_binding_test_fixture.hpp"
#include "score/mw/com/impl/com_error.h"
#include "score/mw/com/impl/configuration/quality_type.h"
#include "score/mw/com/impl/find_service_handle.h"
#include "score/mw/com/impl/find_service_handler.h"
#include "score/mw/com/impl/handle_type.h"
#include "score/mw/com/impl/instance_specifier.h"
#include "score/mw/com/impl/methods/proxy_method_binding.h"
#include "score/mw/com/impl/methods/skeleton_method_binding.h"
#include "score/socom/error.hpp"
#include "score/socom/runtime_mock.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/server_connector_mock.hpp"

namespace score::gateway_ipc_binding::mw_com {
namespace {

using ::testing::_;
using ::testing::A;
using ::testing::ByMove;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

constexpr std::string_view kEmptySpecifier{""};
constexpr std::string_view kClientIdentifier{"client0"};
constexpr std::size_t kEmptyConfigSize{0U};
constexpr std::size_t kFirstEventIndex{0U};
constexpr std::size_t kSingleEventSizeCount{1U};
constexpr std::size_t kExtraBytes{1U};
constexpr std::size_t kOversizedConfigBytes{kMax_service_configuration_size + kExtraBytes};
constexpr std::size_t kSmallGatewayConfigSize{4U};
constexpr std::byte kConfigByte0{0x01U};
constexpr std::byte kConfigByte1{0x02U};
constexpr std::byte kConfigByte2{0x03U};
constexpr std::byte kConfigByte3{0x04U};

constexpr std::uint16_t kSampleServiceId{1U};
constexpr std::uint16_t kSampleEventId{2U};
constexpr std::uint32_t kSampleMaxPayloadSize{16U};
constexpr std::uint16_t kAlternateServiceId{999U};
constexpr std::uint16_t kAlternateEventId{3U};
constexpr std::uint32_t kAlternateMaxPayloadSize{32U};
constexpr std::uint16_t kUnknownServiceId{999U};
constexpr std::uint16_t kUnknownEventId{999U};

constexpr std::uint32_t kServerFindServiceUid{5U};
constexpr std::uint32_t kBridgedConsumerFindServiceUid{10U};
constexpr std::uint32_t kSomeipdFindServiceUid{11U};

template <typename T>
score::cpp::span<std::byte> AsWritableBytes(T& value) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    return score::cpp::span<std::byte>{reinterpret_cast<std::byte*>(&value), sizeof(T)};
}

bool AcceptAllConfigurations(Service_configuration const&) noexcept { return true; }

bool AcceptAllGatewayConfigs(score::cpp::span<std::byte const>,
                             Add_bridged_services const&) noexcept {
    return true;
}

score::Result<socom::Disabled_server_connector::Uptr> MakeDummyServerConnector(
    socom::Server_service_interface_definition, socom::Service_instance,
    socom::Disabled_server_connector::Callbacks) {
    return std::make_unique<NiceMock<socom::Server_connector_mock>>();
}

class SomeipdServiceBindingTest : public IpcBindingTestFixture {
   protected:
    void ExpectSomeipdSkeletonOfferSuccess() {
        auto skel_mock = std::make_unique<NiceMock<score::mw::com::impl::mock_binding::Skeleton>>();
        auto* const skel_mock_ptr = skel_mock.get();
        auto method_mock =
            std::make_unique<NiceMock<score::mw::com::impl::mock_binding::SkeletonMethod>>();
        EXPECT_CALL(*method_mock, RegisterHandler(_))
            .WillOnce([this](score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler&& cb)
                          -> score::Result<void> {
                captured_method_handler_ = std::move(cb);
                return {};
            });
        EXPECT_CALL(*skel_mock_ptr, PrepareOffer(_, _, _)).WillOnce(Return(score::Result<void>{}));
        EXPECT_CALL(*skel_mock_ptr, VerifyAllMethodHandlersRegistered()).WillOnce(Return(true));
        EXPECT_CALL(service_discovery_mock(), OfferService(_))
            .WillOnce(Return(score::Result<void>{}));
        EXPECT_CALL(service_discovery_mock(), StopOfferService(_))
            .WillOnce(Return(score::Result<void>{}));
        EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
            .WillOnce(Return(ByMove(std::move(skel_mock))));
        EXPECT_CALL(skeleton_method_factory_mock(), Create(_, _, _, _, _, _))
            .WillOnce(Return(ByMove(std::move(method_mock))));
    }

    bool InvokeAddServiceConfiguration(Service_configuration& cfg) {
        bool result{false};
        captured_method_handler_(score::mw::com::impl::QualityType::kASIL_QM, AsWritableBytes(cfg),
                                 AsWritableBytes(result));
        return result;
    }

    std::unique_ptr<Gateway_ipc_binding_client> CreateTestClient() {
        EXPECT_CALL(socom_runtime_, make_server_connector(_, _, _))
            .WillOnce(&MakeDummyServerConnector);
        EXPECT_CALL(service_discovery_mock(),
                    StartFindService(_, A<score::mw::com::impl::InstanceSpecifier>()))
            .WillOnce(Return(
                score::mw::com::impl::make_FindServiceHandle(kBridgedConsumerFindServiceUid)))
            .WillOnce(
                [this](
                    score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType> cb,
                    score::mw::com::impl::InstanceSpecifier)
                    -> score::Result<score::mw::com::impl::FindServiceHandle> {
                    someipd_find_handler_ = std::move(cb);
                    return score::mw::com::impl::make_FindServiceHandle(kSomeipdFindServiceUid);
                });
        EXPECT_CALL(service_discovery_mock(), StopFindService(_))
            .WillRepeatedly(Return(score::Result<void>{}));
        constexpr std::array<std::byte, kSmallGatewayConfigSize> kSmallGwCfg{
            kConfigByte0, kConfigByte1, kConfigByte2, kConfigByte3};
        return create_client(socom_runtime_, std::string{kSomeipdSpecifier},
                             {MakeServiceConfig(Role::consumer)}, kClientIdentifier, kSmallGwCfg);
    }

    void NotifySomeipdFound() {
        score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType> const
            handles{MakeHandle()};
        someipd_find_handler_(handles,
                              score::mw::com::impl::make_FindServiceHandle(kSomeipdFindServiceUid));
    }

    void NotifySomeipdLost() {
        someipd_find_handler_(
            score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType>{},
            score::mw::com::impl::make_FindServiceHandle(kSomeipdFindServiceUid));
    }

    void ExpectSomeipdProxyCallResult(bool const accepted) {
        auto proxy_mock = std::make_unique<NiceMock<score::mw::com::impl::mock_binding::Proxy>>();
        ON_CALL(*proxy_mock, SetupMethods(_)).WillByDefault(Return(score::Result<void>{}));
        auto method_mock =
            std::make_unique<NiceMock<score::mw::com::impl::mock_binding::ProxyMethod>>();
        ON_CALL(*method_mock, GetInArgsBuffer(_))
            .WillByDefault(Return(AsWritableBytes(*in_arg_buffer_)));
        ON_CALL(*method_mock, GetReturnValueBuffer(_))
            .WillByDefault(Return(AsWritableBytes(return_value_buffer_)));
        EXPECT_CALL(*method_mock, DoCall(_))
            .WillOnce([this, accepted](std::size_t) -> score::Result<void> {
                return_value_buffer_ = accepted;
                return {};
            });
        EXPECT_CALL(proxy_binding_factory_mock(), Create(_))
            .WillOnce(Return(ByMove(std::move(proxy_mock))));
        EXPECT_CALL(proxy_method_factory_mock(), Create(_, _, _, _, _, _))
            .WillOnce(Return(ByMove(std::move(method_mock))));
    }

    StrictMock<socom::Runtime_mock> socom_runtime_{};

   private:
    score::mw::com::impl::SkeletonMethodBinding::TypeErasedHandler captured_method_handler_{};
    score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>
        someipd_find_handler_{};
    std::unique_ptr<Service_configuration> in_arg_buffer_{
        std::make_unique<Service_configuration>()};
    bool return_value_buffer_{false};
};

// C++17 requires a hand-written operator==; verifies service_id, event_id, and max_payload_size
// are all compared.
TEST(BridgedEventSizeTest, EqualityComparesAllFields) {
    constexpr Bridged_event_size kBaseline{kSampleServiceId, kSampleEventId, kSampleMaxPayloadSize};
    constexpr Bridged_event_size kIdentical{kSampleServiceId, kSampleEventId,
                                            kSampleMaxPayloadSize};
    constexpr Bridged_event_size kDifferentServiceId{kAlternateServiceId, kSampleEventId,
                                                     kSampleMaxPayloadSize};
    constexpr Bridged_event_size kDifferentEventId{kSampleServiceId, kAlternateEventId,
                                                   kSampleMaxPayloadSize};
    constexpr Bridged_event_size kDifferentPayloadSize{kSampleServiceId, kSampleEventId,
                                                       kAlternateMaxPayloadSize};

    EXPECT_TRUE(kBaseline == kIdentical);
    EXPECT_FALSE(kBaseline == kDifferentServiceId);
    EXPECT_FALSE(kBaseline == kDifferentEventId);
    EXPECT_FALSE(kBaseline == kDifferentPayloadSize);
}

// Returns nullptr when someipd_service_specifier is empty.
TEST_F(SomeipdServiceBindingTest, CreateServerReturnsNullptrOnEmptySpecifier) {
    EXPECT_EQ(create_server(socom_runtime_, std::string{kEmptySpecifier}, &AcceptAllGatewayConfigs),
              nullptr);
}

// Invalid specifier syntax returns logic_error_invalid_instance_specifier, not a generic creation
// error.
TEST_F(SomeipdServiceBindingTest, ProviderCreateRejectsInvalidSpecifier) {
    auto const result =
        Someipd_service_provider::create(std::string{kInvalidSpecifier}, &AcceptAllConfigurations);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::logic_error_invalid_instance_specifier);
}

// Fails server->start() when the SomeipdService specifier is missing from mw_com_config.json.
TEST_F(SomeipdServiceBindingTest, ServerStartFailsWhenInstanceNotResolved) {
    ExpectInstanceNotResolved();
    auto server =
        create_server(socom_runtime_, std::string{kSomeipdSpecifier}, &AcceptAllGatewayConfigs);
    ASSERT_NE(server, nullptr);

    auto const start_result = server->start();
    ASSERT_FALSE(start_result.has_value());
    EXPECT_EQ(start_result.error(),
              Mw_com_binding_error::runtime_error_someipd_service_creation_failed);
}

// Fails Someipd_service_provider::create() if RegisterHandler() fails, since LoLa aborts
// OfferService() when a method has no handler.
TEST_F(SomeipdServiceBindingTest, ProviderCreateFailsWhenRegisterHandlerFails) {
    ExpectInstanceResolved();
    auto skel_mock = std::make_unique<NiceMock<score::mw::com::impl::mock_binding::Skeleton>>();
    auto method_mock =
        std::make_unique<NiceMock<score::mw::com::impl::mock_binding::SkeletonMethod>>();
    EXPECT_CALL(*method_mock, RegisterHandler(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
        .WillOnce(Return(ByMove(std::move(skel_mock))));
    EXPECT_CALL(skeleton_method_factory_mock(), Create(_, _, _, _, _, _))
        .WillOnce(Return(ByMove(std::move(method_mock))));

    auto const result =
        Someipd_service_provider::create(std::string{kSomeipdSpecifier}, &AcceptAllConfigurations);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::runtime_error_someipd_service_creation_failed);
}

// Returns runtime_error_someipd_service_offer_failed when OfferService() fails in server->start().
TEST_F(SomeipdServiceBindingTest, ServerStartFailsWhenOfferServiceFails) {
    ExpectInstanceResolved();
    auto skel_mock = std::make_unique<NiceMock<score::mw::com::impl::mock_binding::Skeleton>>();
    auto* const skel_mock_ptr = skel_mock.get();
    auto method_mock =
        std::make_unique<NiceMock<score::mw::com::impl::mock_binding::SkeletonMethod>>();
    ON_CALL(*method_mock, RegisterHandler(_)).WillByDefault(Return(score::Result<void>{}));
    EXPECT_CALL(*skel_mock_ptr, PrepareOffer(_, _, _))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
        .WillOnce(Return(ByMove(std::move(skel_mock))));
    EXPECT_CALL(skeleton_method_factory_mock(), Create(_, _, _, _, _, _))
        .WillOnce(Return(ByMove(std::move(method_mock))));

    auto server =
        create_server(socom_runtime_, std::string{kSomeipdSpecifier}, &AcceptAllGatewayConfigs);
    auto const offer_result = server->start();
    ASSERT_FALSE(offer_result.has_value());
    EXPECT_EQ(offer_result.error(),
              Mw_com_binding_error::runtime_error_someipd_service_offer_failed);
}

// Calling server->start() a second time returns logic_error_already_started without replacing the
// active skeleton.
TEST_F(SomeipdServiceBindingTest, ServerSecondStartReturnsAlreadyStartedError) {
    ExpectInstanceResolved();
    ExpectSomeipdSkeletonOfferSuccess();

    auto server =
        create_server(socom_runtime_, std::string{kSomeipdSpecifier}, &AcceptAllGatewayConfigs);
    ASSERT_TRUE(server->start().has_value());

    auto const second_start = server->start();
    ASSERT_FALSE(second_start.has_value());
    EXPECT_EQ(second_start.error(), Mw_com_binding_error::logic_error_already_started);
}

// Rejects gateway_config.size > max_size before reading past the SHM array, and returns false when
// the configuration callback returns false.
TEST_F(SomeipdServiceBindingTest, ServerRejectsOversizedAndHandlerRejectedConfigurations) {
    ExpectInstanceResolved();
    ExpectSomeipdSkeletonOfferSuccess();

    bool handler_accepts{false};
    auto server =
        create_server(socom_runtime_, std::string{kSomeipdSpecifier},
                      [&handler_accepts](score::cpp::span<std::byte const>,
                                         Add_bridged_services const&) { return handler_accepts; });
    ASSERT_TRUE(server->start().has_value());

    auto cfg = std::make_unique<Service_configuration>();
    cfg->gateway_config.size = kOversizedConfigBytes;
    EXPECT_FALSE(InvokeAddServiceConfiguration(*cfg));

    cfg->gateway_config.size = kEmptyConfigSize;
    EXPECT_FALSE(InvokeAddServiceConfiguration(*cfg));
}

// Rejects provider configurations when event_sizes has the wrong count or an unknown
// (service_id, event_id) pair.
TEST_F(SomeipdServiceBindingTest, ServerRejectsMismatchedAndUnknownProviderEventSizes) {
    ExpectInstanceResolved();
    ExpectSomeipdSkeletonOfferSuccess();

    auto server = create_server(
        socom_runtime_, std::string{kSomeipdSpecifier},
        [](score::cpp::span<std::byte const>, Add_bridged_services const& add_bridged_services) {
            return add_bridged_services({MakeServiceConfig(Role::provider)});
        });
    ASSERT_TRUE(server->start().has_value());

    auto cfg = std::make_unique<Service_configuration>();
    cfg->event_sizes.size = kEmptyConfigSize;
    EXPECT_FALSE(InvokeAddServiceConfiguration(*cfg));

    cfg->event_sizes.size = kSingleEventSizeCount;
    cfg->event_sizes.data[kFirstEventIndex] =
        Bridged_event_size{kUnknownServiceId, kUnknownEventId, kSampleMaxPayloadSize};
    EXPECT_FALSE(InvokeAddServiceConfiguration(*cfg));
}

// Returns true without creating duplicate bindings when a restarted gatewayd sends the same
// configuration again.
TEST_F(SomeipdServiceBindingTest, ServerAcceptsValidConsumerConfigAndShortCircuitsDuplicate) {
    ExpectInstanceResolved();
    ExpectSomeipdSkeletonOfferSuccess();

    auto server = create_server(
        socom_runtime_, std::string{kSomeipdSpecifier},
        [](score::cpp::span<std::byte const>, Add_bridged_services const& add_bridged_services) {
            return add_bridged_services({MakeServiceConfig(Role::consumer)});
        });
    ASSERT_TRUE(server->start().has_value());

    EXPECT_CALL(socom_runtime_, make_server_connector(_, _, _)).WillOnce(&MakeDummyServerConnector);
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, A<score::mw::com::impl::InstanceSpecifier>()))
        .WillRepeatedly(
            Return(score::mw::com::impl::make_FindServiceHandle(kServerFindServiceUid)));
    EXPECT_CALL(service_discovery_mock(), StopFindService(_))
        .WillRepeatedly(Return(score::Result<void>{}));

    auto cfg = std::make_unique<Service_configuration>();
    EXPECT_TRUE(InvokeAddServiceConfiguration(*cfg));
    EXPECT_TRUE(InvokeAddServiceConfiguration(*cfg));
}

// Invalid or undeployed SomeipdService specifier fails immediately instead of waiting indefinitely
// in StartFindService().
TEST_F(SomeipdServiceBindingTest, ConsumerCreateRejectsInvalidAndUndeployedSpecifier) {
    auto const cfg = std::make_unique<Service_configuration>();

    auto const invalid_res = Someipd_service_consumer::create(std::string{kInvalidSpecifier}, *cfg);
    ASSERT_FALSE(invalid_res.has_value());
    EXPECT_EQ(invalid_res.error(), Mw_com_binding_error::logic_error_invalid_instance_specifier);

    ExpectInstanceNotResolved();
    auto const undeployed_res =
        Someipd_service_consumer::create(std::string{kSomeipdSpecifier}, *cfg);
    ASSERT_FALSE(undeployed_res.has_value());
    EXPECT_EQ(undeployed_res.error(), Mw_com_binding_error::logic_error_invalid_instance_specifier);
}

// Returns runtime_error_someipd_service_find_failed when StartFindService() fails.
TEST_F(SomeipdServiceBindingTest, ConsumerCreateFailsWhenStartFindServiceFails) {
    ExpectInstanceResolved();
    auto const cfg = std::make_unique<Service_configuration>();
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, A<score::mw::com::impl::InstanceSpecifier>()))
        .WillOnce(Return(
            score::MakeUnexpected(score::mw::com::impl::ComErrc::kFindServiceHandlerFailure)));

    auto const start_fail = Someipd_service_consumer::create(std::string{kSomeipdSpecifier}, *cfg);
    ASSERT_FALSE(start_fail.has_value());
    EXPECT_EQ(start_fail.error(), Mw_com_binding_error::runtime_error_someipd_service_find_failed);
}

// Returns nullptr if a bridged service config is invalid, gateway_config exceeds the SHM array
// capacity, or SOCom connector creation fails.
TEST_F(SomeipdServiceBindingTest,
       CreateClientRejectsInvalidConfigOversizedPayloadAndFailedBinding) {
    ExpectInstanceResolved();

    auto bad_service = MakeServiceConfig();
    bad_service.events.clear();
    EXPECT_EQ(create_client(socom_runtime_, std::string{kSomeipdSpecifier}, {bad_service}),
              nullptr);

    std::vector<std::byte> const huge_config(kOversizedConfigBytes);
    EXPECT_EQ(create_client(socom_runtime_, std::string{kSomeipdSpecifier}, {}, kClientIdentifier,
                            huge_config),
              nullptr);

    EXPECT_CALL(socom_runtime_, make_server_connector(_, _, _))
        .WillOnce(Return(ByMove(score::MakeUnexpected<socom::Disabled_server_connector::Uptr>(
            socom::Construction_error::duplicate_service))));
    EXPECT_EQ(create_client(socom_runtime_, std::string{kSomeipdSpecifier},
                            {MakeServiceConfig(Role::consumer)}),
              nullptr);
}

// is_connected() returns true only after AddServiceConfiguration returns true, and returns false
// again when SomeipdService stops offering.
TEST_F(SomeipdServiceBindingTest,
       ClientConnectionStateTracksSomeipdDiscoveryAndConfigurationReply) {
    ExpectInstanceResolved();
    auto client = CreateTestClient();
    ASSERT_NE(client, nullptr);
    EXPECT_FALSE(client->is_connected());

    EXPECT_CALL(proxy_binding_factory_mock(), Create(_)).WillOnce(Return(ByMove(nullptr)));
    NotifySomeipdFound();
    EXPECT_FALSE(client->is_connected());

    ExpectSomeipdProxyCallResult(false);
    NotifySomeipdFound();
    EXPECT_FALSE(client->is_connected());

    ExpectSomeipdProxyCallResult(true);
    NotifySomeipdFound();
    EXPECT_TRUE(client->is_connected());

    NotifySomeipdLost();
    EXPECT_FALSE(client->is_connected());

    EXPECT_CALL(service_discovery_mock(), StopFindService(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kInvalidHandle)))
        .WillOnce(Return(score::Result<void>{}));
    client.reset();
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
