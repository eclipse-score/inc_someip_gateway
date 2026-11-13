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

#ifndef SCORE_TEST_MW_COM_TEST_FIXTURE_HPP
#define SCORE_TEST_MW_COM_TEST_FIXTURE_HPP

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "score/mw/com/impl/configuration/lola_event_instance_deployment.h"
#include "score/mw/com/impl/configuration/lola_service_instance_deployment.h"
#include "score/mw/com/impl/configuration/lola_service_instance_id.h"
#include "score/mw/com/impl/configuration/lola_service_type_deployment.h"
#include "score/mw/com/impl/configuration/quality_type.h"
#include "score/mw/com/impl/configuration/service_identifier_type.h"
#include "score/mw/com/impl/configuration/service_instance_deployment.h"
#include "score/mw/com/impl/configuration/service_instance_id.h"
#include "score/mw/com/impl/configuration/service_type_deployment.h"
#include "score/mw/com/impl/handle_type.h"
#include "score/mw/com/impl/instance_identifier.h"
#include "score/mw/com/impl/instance_specifier.h"
#include "score/result/result.h"
#include "score/socom/client_connector.hpp"
#include "score/socom/client_connector_mock.hpp"
#include "score/socom/runtime_mock.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/server_connector_mock.hpp"
#include "score/test/mw_com_mocks.hpp"

namespace score::testing {

inline constexpr std::uint16_t kInitialLolaEventId{1U};
inline constexpr std::uint16_t kDefaultLolaSampleSlots{1U};
inline constexpr std::uint8_t kDefaultLolaMaxSubscribers{1U};
inline constexpr std::uint8_t kDefaultLolaMaxConcurrentAllocations{1U};
inline constexpr bool kDefaultLolaEnforceMaxSamples{true};
inline constexpr std::uint8_t kDefaultLolaMaxTracingSubscribers{0U};

struct ServiceDeploymentSpec {
    std::string_view type_name{"test_type"};
    std::uint16_t service_id{100U};
    std::uint8_t major_version{1U};
    std::uint32_t minor_version{0U};
    std::uint16_t instance_id{1U};
};

class DeployedInstanceHolder {
   public:
    DeployedInstanceHolder() = default;

    DeployedInstanceHolder(DeployedInstanceHolder const&) = delete;
    DeployedInstanceHolder& operator=(DeployedInstanceHolder const&) = delete;
    DeployedInstanceHolder(DeployedInstanceHolder&&) = delete;
    DeployedInstanceHolder& operator=(DeployedInstanceHolder&&) = delete;

    score::mw::com::impl::InstanceIdentifier MakeIdentifier(
        std::string_view const specifier, std::initializer_list<std::string_view> const event_names,
        ServiceDeploymentSpec const& spec = {}) {
        score::mw::com::impl::LolaServiceTypeDeployment::EventIdMapping type_events{};
        score::mw::com::impl::LolaServiceInstanceDeployment::EventInstanceMapping instance_events{};
        std::uint16_t next_id{kInitialLolaEventId};
        for (auto const name : event_names) {
            std::string event_name{name};
            type_events.emplace(event_name, next_id);
            ++next_id;
            instance_events.emplace(
                std::move(event_name),
                score::mw::com::impl::LolaEventInstanceDeployment{
                    kDefaultLolaSampleSlots, kDefaultLolaMaxSubscribers,
                    kDefaultLolaMaxConcurrentAllocations, kDefaultLolaEnforceMaxSamples,
                    kDefaultLolaMaxTracingSubscribers});
        }
        auto const& type_deployment =
            type_deployments_.emplace_back(score::mw::com::impl::LolaServiceTypeDeployment{
                spec.service_id, std::move(type_events), {}});
        auto const& instance_deployment = instance_deployments_.emplace_back(
            score::mw::com::impl::make_ServiceIdentifierType(
                std::string{spec.type_name}, spec.major_version, spec.minor_version),
            score::mw::com::impl::LolaServiceInstanceDeployment{
                score::mw::com::impl::LolaServiceInstanceId{spec.instance_id},
                std::move(instance_events),
                {},
                {}},
            score::mw::com::impl::QualityType::kASIL_QM,
            score::mw::com::impl::InstanceSpecifier::Create(std::string{specifier}).value());
        return score::mw::com::impl::make_InstanceIdentifier(instance_deployment, type_deployment);
    }

    score::mw::com::impl::HandleType MakeHandle(
        std::string_view const specifier, std::initializer_list<std::string_view> const event_names,
        ServiceDeploymentSpec const& spec = {}) {
        return score::mw::com::impl::make_HandleType(
            MakeIdentifier(specifier, event_names, spec),
            score::mw::com::impl::ServiceInstanceId{
                score::mw::com::impl::LolaServiceInstanceId{spec.instance_id}});
    }

   private:
    std::deque<score::mw::com::impl::ServiceTypeDeployment> type_deployments_{};
    std::deque<score::mw::com::impl::ServiceInstanceDeployment> instance_deployments_{};
};

struct MockSkeletonWithEvent {
    ::testing::NiceMock<Skeleton>* skeleton{nullptr};
    ::testing::NiceMock<GenericSkeletonEvent>* event{nullptr};
};

struct MockSkeletonWithMethod {
    ::testing::NiceMock<Skeleton>* skeleton{nullptr};
    ::testing::NiceMock<SkeletonMethod>* method{nullptr};
};

class MwComTestFixture : public ::testing::Test {
   protected:
    MwComTestFixture() {
        ON_CALL(runtime_mock_guard_.mock(), GetServiceDiscovery())
            .WillByDefault(::testing::ReturnRef(service_discovery_mock_));
    }

    DeployedInstanceHolder& instance_holder() noexcept { return instance_holder_; }

    ::testing::NiceMock<RuntimeMock>& runtime_mock() noexcept { return runtime_mock_guard_.mock(); }

    ::testing::NiceMock<ServiceDiscoveryMock>& service_discovery_mock() noexcept {
        return service_discovery_mock_;
    }

    ::testing::NiceMock<ProxyBindingFactoryMock>& proxy_binding_factory_mock() noexcept {
        return proxy_binding_factory_guard_.mock();
    }

    ::testing::NiceMock<GenericProxyEventBindingFactoryMock>&
    generic_proxy_event_factory_mock() noexcept {
        return generic_proxy_event_factory_guard_.mock();
    }

    ::testing::NiceMock<ProxyMethodBindingFactoryMock>& proxy_method_factory_mock() noexcept {
        return proxy_method_factory_guard_.mock();
    }

    ::testing::NiceMock<SkeletonBindingFactoryMock>& skeleton_binding_factory_mock() noexcept {
        return skeleton_binding_factory_guard_.mock();
    }

    ::testing::NiceMock<SkeletonEventBindingFactoryMock>&
    generic_skeleton_event_factory_mock() noexcept {
        return generic_skeleton_event_factory_guard_.mock();
    }

    ::testing::NiceMock<SkeletonMethodBindingFactoryMock>& skeleton_method_factory_mock() noexcept {
        return skeleton_method_factory_guard_.mock();
    }

    void StubInstanceResolved(std::string_view const specifier,
                              std::initializer_list<std::string_view> const events,
                              ServiceDeploymentSpec const& spec) {
        auto const id = instance_holder_.MakeIdentifier(specifier, events, spec);
        ON_CALL(runtime_mock_guard_.mock(), resolve(::testing::_))
            .WillByDefault(
                ::testing::Return(std::vector<score::mw::com::impl::InstanceIdentifier>{id}));
    }

    void StubInstanceNotResolved() {
        ON_CALL(runtime_mock_guard_.mock(), resolve(::testing::_))
            .WillByDefault(
                ::testing::Return(std::vector<score::mw::com::impl::InstanceIdentifier>{}));
    }

    ::testing::NiceMock<GenericProxyEvent>* ExpectProxyWithEvent(
        std::string_view const event_name) {
        auto proxy_mock = std::make_unique<::testing::NiceMock<Proxy>>();
        auto event_mock = std::make_unique<::testing::NiceMock<GenericProxyEvent>>();
        auto* const event_ptr = event_mock.get();
        EXPECT_CALL(*proxy_mock, IsEventProvided(event_name)).WillOnce(::testing::Return(true));
        EXPECT_CALL(proxy_binding_factory_mock(), Create(::testing::_))
            .WillOnce(::testing::Return(::testing::ByMove(std::move(proxy_mock))));
        EXPECT_CALL(generic_proxy_event_factory_mock(),
                    Create(::testing::_, ::testing::_, event_name, ::testing::_))
            .WillOnce(::testing::Return(::testing::ByMove(std::move(event_mock))));
        return event_ptr;
    }

    MockSkeletonWithEvent ExpectSkeletonWithEvent(std::string_view const event_name) {
        auto skel_mock = std::make_unique<::testing::NiceMock<Skeleton>>();
        auto event_mock = std::make_unique<::testing::NiceMock<GenericSkeletonEvent>>();
        MockSkeletonWithEvent const mocks{skel_mock.get(), event_mock.get()};
        EXPECT_CALL(skeleton_binding_factory_mock(), Create(::testing::_))
            .WillOnce(::testing::Return(::testing::ByMove(std::move(skel_mock))));
        EXPECT_CALL(generic_skeleton_event_factory_mock(),
                    Create(::testing::_, ::testing::_, event_name, ::testing::_))
            .WillOnce(::testing::Return(::testing::ByMove(std::move(event_mock))));
        return mocks;
    }

    MockSkeletonWithMethod ExpectSkeletonWithMethod() {
        auto skel_mock = std::make_unique<::testing::NiceMock<Skeleton>>();
        auto method_mock = std::make_unique<::testing::NiceMock<SkeletonMethod>>();
        MockSkeletonWithMethod const mocks{skel_mock.get(), method_mock.get()};
        EXPECT_CALL(skeleton_binding_factory_mock(), Create(::testing::_))
            .WillOnce(::testing::Return(::testing::ByMove(std::move(skel_mock))));
        EXPECT_CALL(skeleton_method_factory_mock(),
                    Create(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_,
                           ::testing::_))
            .WillOnce(::testing::Return(::testing::ByMove(std::move(method_mock))));
        return mocks;
    }

    ::testing::NiceMock<socom::Server_connector_mock>* ExpectServerConnectorCreated(
        socom::Disabled_server_connector::Callbacks* const out_callbacks = nullptr) {
        auto sc_mock = std::make_unique<::testing::NiceMock<socom::Server_connector_mock>>();
        auto* const sc_ptr = sc_mock.get();
        ON_CALL(*sc_ptr, enable()).WillByDefault(::testing::Return(sc_ptr));
        ON_CALL(*sc_ptr, disable()).WillByDefault(::testing::Return(sc_ptr));
        EXPECT_CALL(socom_runtime_, make_server_connector(::testing::_, ::testing::_, ::testing::_))
            .WillOnce([sc = std::move(sc_mock), out_callbacks](
                          socom::Server_service_interface_definition, socom::Service_instance,
                          socom::Disabled_server_connector::Callbacks callbacks) mutable
                          -> score::Result<socom::Disabled_server_connector::Uptr> {
                if (out_callbacks != nullptr) {
                    *out_callbacks = std::move(callbacks);
                }
                return std::move(sc);
            });
        return sc_ptr;
    }

    ::testing::NiceMock<socom::Client_connector_mock>* ExpectClientConnectorCreated(
        socom::Client_connector::Callbacks* const out_callbacks = nullptr) {
        auto cc_mock = std::make_unique<::testing::NiceMock<socom::Client_connector_mock>>();
        auto* const cc_ptr = cc_mock.get();
        EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
            .WillOnce([cc = std::move(cc_mock), out_callbacks](
                          socom::Service_interface_definition, socom::Service_instance,
                          socom::Client_connector::Callbacks callbacks) mutable
                          -> score::Result<socom::Client_connector::Uptr> {
                if (out_callbacks != nullptr) {
                    *out_callbacks = std::move(callbacks);
                }
                return std::move(cc);
            });
        return cc_ptr;
    }

    ::testing::StrictMock<socom::Runtime_mock> socom_runtime_{};

   private:
    DeployedInstanceHolder instance_holder_{};
    ::testing::NiceMock<ServiceDiscoveryMock> service_discovery_mock_{};
    RuntimeMockGuard runtime_mock_guard_{};
    ProxyBindingFactoryMockGuard proxy_binding_factory_guard_{};
    GenericProxyEventBindingFactoryMockGuard generic_proxy_event_factory_guard_{};
    ProxyMethodBindingFactoryMockGuard proxy_method_factory_guard_{};
    SkeletonBindingFactoryMockGuard skeleton_binding_factory_guard_{};
    SkeletonEventBindingFactoryMockGuard generic_skeleton_event_factory_guard_{};
    SkeletonMethodBindingFactoryMockGuard skeleton_method_factory_guard_{};
};

}  // namespace score::testing

#endif  // SCORE_TEST_MW_COM_TEST_FIXTURE_HPP
