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

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <memory>
#include <score/span.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "score/mw/com/impl/com_error.h"
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
inline constexpr std::uint16_t kDefaultLolaSampleSlots{20U};
inline constexpr std::uint8_t kDefaultLolaMaxSubscribers{1U};
inline constexpr std::uint8_t kDefaultLolaMaxConcurrentAllocations{20U};
inline constexpr bool kDefaultLolaEnforceMaxSamples{true};
inline constexpr std::uint8_t kDefaultLolaMaxTracingSubscribers{0U};

struct ServiceDeploymentSpec {
    std::string_view type_name{"test_type"};
    std::uint16_t service_id{100U};
    std::uint8_t major_version{1U};
    std::uint32_t minor_version{0U};
    std::uint16_t instance_id{1U};
    std::uint16_t sample_slots{kDefaultLolaSampleSlots};
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
                    spec.sample_slots, kDefaultLolaMaxSubscribers,
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

struct MockProxyWithEvents {
    ::testing::NiceMock<Proxy>* proxy{nullptr};
    std::vector<::testing::NiceMock<GenericProxyEvent>*> events{};
};

struct MockProxyWithMethod {
    ::testing::NiceMock<Proxy>* proxy{nullptr};
    ::testing::NiceMock<ProxyMethod>* method{nullptr};
};

namespace detail {

class MethodCallStorageBase {
   public:
    MethodCallStorageBase() = default;
    virtual ~MethodCallStorageBase() = default;
    MethodCallStorageBase(MethodCallStorageBase const&) = delete;
    MethodCallStorageBase& operator=(MethodCallStorageBase const&) = delete;
    MethodCallStorageBase(MethodCallStorageBase&&) = delete;
    MethodCallStorageBase& operator=(MethodCallStorageBase&&) = delete;
};

template <typename InArg, typename ReturnType>
class MethodCallStorage final : public MethodCallStorageBase {
   public:
    static_assert(std::is_trivially_copyable_v<InArg>,
                  "mw::com method InArg must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<ReturnType>,
                  "mw::com method ReturnType must be trivially copyable");

    InArg in_arg{};
    ReturnType return_value{};

    score::cpp::span<std::byte> in_arg_span() noexcept {
        return score::cpp::span<std::byte>{static_cast<std::byte*>(static_cast<void*>(&in_arg)),
                                           sizeof(InArg)};
    }

    score::cpp::span<std::byte> return_span() noexcept {
        return score::cpp::span<std::byte>{
            static_cast<std::byte*>(static_cast<void*>(&return_value)), sizeof(ReturnType)};
    }
};

}  // namespace detail

template <typename InArg, typename ReturnType>
struct TypedMethodCallBuffers {
    InArg* in_arg{nullptr};
    ReturnType* return_value{nullptr};
    score::cpp::span<std::byte> in_arg_bytes{};
    score::cpp::span<std::byte> return_bytes{};
};

template <typename InArg, typename ReturnType>
struct MockTypedProxyWithMethod {
    ::testing::NiceMock<Proxy>* proxy{nullptr};
    ::testing::NiceMock<ProxyMethod>* method{nullptr};
    TypedMethodCallBuffers<InArg, ReturnType> buffers{};
};

struct MockSkeletonWithEvent {
    ::testing::NiceMock<Skeleton>* skeleton{nullptr};
    ::testing::NiceMock<GenericSkeletonEvent>* event{nullptr};
};

struct MockSkeletonWithEvents {
    ::testing::NiceMock<Skeleton>* skeleton{nullptr};
    std::vector<::testing::NiceMock<GenericSkeletonEvent>*> events{};
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
        ON_CALL(runtime_mock_guard_.mock(), resolve(::testing::_))
            .WillByDefault(
                ::testing::Return(std::vector<score::mw::com::impl::InstanceIdentifier>{}));
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
        auto const expected_specifier =
            score::mw::com::impl::InstanceSpecifier::Create(std::string{specifier}).value();
        auto const id = instance_holder_.MakeIdentifier(specifier, events, spec);
        ON_CALL(runtime_mock_guard_.mock(), resolve(expected_specifier))
            .WillByDefault(
                ::testing::Return(std::vector<score::mw::com::impl::InstanceIdentifier>{id}));
    }

    void StubInstanceNotResolved() {
        ON_CALL(runtime_mock_guard_.mock(), resolve(::testing::_))
            .WillByDefault(
                ::testing::Return(std::vector<score::mw::com::impl::InstanceIdentifier>{}));
    }

    void StubInstanceNotResolved(std::string_view const specifier) {
        auto const expected_specifier =
            score::mw::com::impl::InstanceSpecifier::Create(std::string{specifier}).value();
        ON_CALL(runtime_mock_guard_.mock(), resolve(expected_specifier))
            .WillByDefault(
                ::testing::Return(std::vector<score::mw::com::impl::InstanceIdentifier>{}));
    }

    ::testing::Sequence& proxy_factory_sequence() noexcept { return proxy_factory_sequence_; }
    ::testing::Sequence& skeleton_factory_sequence() noexcept { return skeleton_factory_sequence_; }
    ::testing::Sequence& server_connector_sequence() noexcept { return server_connector_sequence_; }
    ::testing::Sequence& client_connector_sequence() noexcept { return client_connector_sequence_; }

    MockProxyWithEvents ExpectProxyWithEvents(
        std::initializer_list<std::string_view> const event_names) {
        auto proxy_mock = std::make_unique<::testing::NiceMock<Proxy>>();
        auto* const proxy_ptr = proxy_mock.get();
        ON_CALL(*proxy_ptr, IsEventProvided(::testing::_)).WillByDefault(::testing::Return(false));
        MockProxyWithEvents mocks{proxy_ptr, {}};
        mocks.events.reserve(event_names.size());
        for (auto const event_name : event_names) {
            auto event_mock = std::make_unique<::testing::NiceMock<GenericProxyEvent>>();
            mocks.events.push_back(event_mock.get());
            EXPECT_CALL(*proxy_ptr, IsEventProvided(event_name))
                .WillOnce(::testing::Return(true))
                .RetiresOnSaturation();
            EXPECT_CALL(generic_proxy_event_factory_mock(),
                        Create(::testing::_, ::testing::Ref(*proxy_ptr), event_name, ::testing::_))
                .WillOnce(::testing::Return(::testing::ByMove(std::move(event_mock))))
                .RetiresOnSaturation();
        }
        EXPECT_CALL(proxy_binding_factory_mock(), Create(::testing::_))
            .InSequence(proxy_factory_sequence_)
            .WillOnce(::testing::Return(::testing::ByMove(std::move(proxy_mock))))
            .RetiresOnSaturation();
        return mocks;
    }

    ::testing::NiceMock<GenericProxyEvent>* ExpectProxyWithEvent(
        std::string_view const event_name) {
        return ExpectProxyWithEvents({event_name}).events.front();
    }

    void ExpectProxyCreationFailure() {
        EXPECT_CALL(proxy_binding_factory_mock(), Create(::testing::_))
            .InSequence(proxy_factory_sequence_)
            .WillOnce(::testing::Return(
                ::testing::ByMove(std::unique_ptr<score::mw::com::impl::ProxyBinding>{})))
            .RetiresOnSaturation();
    }

    MockProxyWithMethod ExpectProxyWithMethod(std::string_view const method_name = {}) {
        auto proxy_mock = std::make_unique<::testing::NiceMock<Proxy>>();
        auto* const proxy_ptr = proxy_mock.get();
        auto method_mock = std::make_unique<::testing::NiceMock<ProxyMethod>>();
        MockProxyWithMethod const mocks{proxy_ptr, method_mock.get()};
        EXPECT_CALL(proxy_binding_factory_mock(), Create(::testing::_))
            .InSequence(proxy_factory_sequence_)
            .WillOnce(::testing::Return(::testing::ByMove(std::move(proxy_mock))))
            .RetiresOnSaturation();
        if (method_name.empty()) {
            EXPECT_CALL(proxy_method_factory_mock(),
                        Create(::testing::_, ::testing::Ref(*proxy_ptr), ::testing::_, ::testing::_,
                               ::testing::_, ::testing::_))
                .WillOnce(::testing::Return(::testing::ByMove(std::move(method_mock))))
                .RetiresOnSaturation();
        } else {
            EXPECT_CALL(proxy_method_factory_mock(),
                        Create(::testing::_, ::testing::Ref(*proxy_ptr), method_name, ::testing::_,
                               ::testing::_, ::testing::_))
                .WillOnce(::testing::Return(::testing::ByMove(std::move(method_mock))))
                .RetiresOnSaturation();
        }
        return mocks;
    }

    template <typename InArg, typename ReturnType>
    TypedMethodCallBuffers<InArg, ReturnType> AllocateMethodCallBuffers() {
        auto storage = std::make_unique<detail::MethodCallStorage<InArg, ReturnType>>();
        TypedMethodCallBuffers<InArg, ReturnType> const buffers{
            &storage->in_arg, &storage->return_value, storage->in_arg_span(),
            storage->return_span()};
        method_call_buffers_.push_back(std::move(storage));
        return buffers;
    }

    template <typename InArg, typename ReturnType>
    MockTypedProxyWithMethod<InArg, ReturnType> ExpectTypedProxyWithMethod(
        std::string_view const method_name = {}) {
        auto const mocks = ExpectProxyWithMethod(method_name);
        auto const buffers = AllocateMethodCallBuffers<InArg, ReturnType>();
        ON_CALL(*mocks.method, GetInArgsBuffer(::testing::_))
            .WillByDefault(::testing::Return(buffers.in_arg_bytes));
        ON_CALL(*mocks.method, GetReturnValueBuffer(::testing::_))
            .WillByDefault(::testing::Return(buffers.return_bytes));
        return MockTypedProxyWithMethod<InArg, ReturnType>{mocks.proxy, mocks.method, buffers};
    }

    MockSkeletonWithEvents ExpectSkeletonWithEvents(
        std::initializer_list<std::string_view> const event_names) {
        auto skel_mock = std::make_unique<::testing::NiceMock<Skeleton>>();
        auto* const skel_ptr = skel_mock.get();
        MockSkeletonWithEvents mocks{skel_ptr, {}};
        mocks.events.reserve(event_names.size());
        EXPECT_CALL(skeleton_binding_factory_mock(), Create(::testing::_))
            .InSequence(skeleton_factory_sequence_)
            .WillOnce(::testing::Return(::testing::ByMove(std::move(skel_mock))))
            .RetiresOnSaturation();
        for (auto const event_name : event_names) {
            auto event_mock = std::make_unique<::testing::NiceMock<GenericSkeletonEvent>>();
            mocks.events.push_back(event_mock.get());
            EXPECT_CALL(generic_skeleton_event_factory_mock(),
                        Create(::testing::_, ::testing::Ref(*skel_ptr), event_name, ::testing::_))
                .WillOnce(::testing::Return(::testing::ByMove(std::move(event_mock))))
                .RetiresOnSaturation();
        }
        return mocks;
    }

    MockSkeletonWithEvent ExpectSkeletonWithEvent(std::string_view const event_name) {
        auto const mocks = ExpectSkeletonWithEvents({event_name});
        return MockSkeletonWithEvent{mocks.skeleton, mocks.events.front()};
    }

    void ExpectSkeletonCreationFailure() {
        EXPECT_CALL(skeleton_binding_factory_mock(), Create(::testing::_))
            .InSequence(skeleton_factory_sequence_)
            .WillOnce(::testing::Return(
                ::testing::ByMove(std::unique_ptr<score::mw::com::impl::SkeletonBinding>{})))
            .RetiresOnSaturation();
    }

    MockSkeletonWithMethod ExpectSkeletonWithMethod(std::string_view const method_name = {}) {
        auto skel_mock = std::make_unique<::testing::NiceMock<Skeleton>>();
        auto* const skel_ptr = skel_mock.get();
        auto method_mock = std::make_unique<::testing::NiceMock<SkeletonMethod>>();
        MockSkeletonWithMethod const mocks{skel_ptr, method_mock.get()};
        EXPECT_CALL(skeleton_binding_factory_mock(), Create(::testing::_))
            .InSequence(skeleton_factory_sequence_)
            .WillOnce(::testing::Return(::testing::ByMove(std::move(skel_mock))))
            .RetiresOnSaturation();
        if (method_name.empty()) {
            EXPECT_CALL(skeleton_method_factory_mock(),
                        Create(::testing::_, ::testing::Ref(*skel_ptr), ::testing::_, ::testing::_,
                               ::testing::_, ::testing::_))
                .WillOnce(::testing::Return(::testing::ByMove(std::move(method_mock))))
                .RetiresOnSaturation();
        } else {
            EXPECT_CALL(skeleton_method_factory_mock(),
                        Create(::testing::_, ::testing::Ref(*skel_ptr), method_name, ::testing::_,
                               ::testing::_, ::testing::_))
                .WillOnce(::testing::Return(::testing::ByMove(std::move(method_mock))))
                .RetiresOnSaturation();
        }
        return mocks;
    }

    ::testing::NiceMock<socom::Server_connector_mock>* ExpectServerConnectorCreated(
        socom::Disabled_server_connector::Callbacks* const out_callbacks = nullptr) {
        auto sc_mock = std::make_unique<::testing::NiceMock<socom::Server_connector_mock>>();
        auto* const sc_ptr = sc_mock.get();
        ON_CALL(*sc_ptr, enable()).WillByDefault(::testing::Return(sc_ptr));
        ON_CALL(*sc_ptr, disable()).WillByDefault(::testing::Return(sc_ptr));
        EXPECT_CALL(socom_runtime_, make_server_connector(::testing::_, ::testing::_, ::testing::_))
            .InSequence(server_connector_sequence_)
            .WillOnce([sc = std::move(sc_mock), out_callbacks](
                          socom::Server_service_interface_definition, socom::Service_instance,
                          socom::Disabled_server_connector::Callbacks callbacks) mutable
                          -> score::Result<socom::Disabled_server_connector::Uptr> {
                if (out_callbacks != nullptr) {
                    *out_callbacks = std::move(callbacks);
                }
                return std::move(sc);
            })
            .RetiresOnSaturation();
        return sc_ptr;
    }

    void ExpectServerConnectorCreationFailure(
        socom::Error const error = socom::Error::runtime_error_request_rejected) {
        EXPECT_CALL(socom_runtime_, make_server_connector(::testing::_, ::testing::_, ::testing::_))
            .InSequence(server_connector_sequence_)
            .WillOnce(::testing::Return(score::MakeUnexpected(error)))
            .RetiresOnSaturation();
    }

    static void StubEnableFiresCallbacks(
        ::testing::NiceMock<socom::Server_connector_mock>& connector,
        std::function<void(socom::Enabled_server_connector&)> on_enable) {
        ON_CALL(connector, enable())
            .WillByDefault([&connector, on_enable = std::move(
                                            on_enable)]() -> socom::Enabled_server_connector* {
                if (on_enable) {
                    on_enable(connector);
                }
                return &connector;
            });
    }

    static void StubEnableSubscribesEvents(
        ::testing::NiceMock<socom::Server_connector_mock>& connector,
        socom::Disabled_server_connector::Callbacks& callbacks,
        std::initializer_list<socom::Event_id> const event_ids) {
        std::vector<socom::Event_id> const ids{event_ids};
        StubEnableFiresCallbacks(connector, [&callbacks,
                                             ids](socom::Enabled_server_connector& enabled) {
            for (auto const id : ids) {
                callbacks.on_event_subscription_change(enabled, id, socom::Event_state::subscribed);
            }
        });
    }

    ::testing::NiceMock<socom::Client_connector_mock>* ExpectClientConnectorCreated(
        socom::Client_connector::Callbacks* const out_callbacks = nullptr) {
        auto cc_mock = std::make_unique<::testing::NiceMock<socom::Client_connector_mock>>();
        auto* const cc_ptr = cc_mock.get();
        EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
            .InSequence(client_connector_sequence_)
            .WillOnce([cc = std::move(cc_mock), out_callbacks](
                          socom::Service_interface_definition, socom::Service_instance,
                          socom::Client_connector::Callbacks callbacks) mutable
                          -> score::Result<socom::Client_connector::Uptr> {
                if (out_callbacks != nullptr) {
                    *out_callbacks = std::move(callbacks);
                }
                return std::move(cc);
            })
            .RetiresOnSaturation();
        return cc_ptr;
    }

    void ExpectClientConnectorCreationFailure(
        socom::Error const error = socom::Error::runtime_error_request_rejected) {
        EXPECT_CALL(socom_runtime_, make_client_connector(::testing::_, ::testing::_, ::testing::_))
            .InSequence(client_connector_sequence_)
            .WillOnce(::testing::Return(score::MakeUnexpected(error)))
            .RetiresOnSaturation();
    }

    ::testing::StrictMock<socom::Runtime_mock> socom_runtime_{};

   private:
    DeployedInstanceHolder instance_holder_{};
    std::vector<std::unique_ptr<detail::MethodCallStorageBase>> method_call_buffers_{};
    ::testing::Sequence proxy_factory_sequence_{};
    ::testing::Sequence skeleton_factory_sequence_{};
    ::testing::Sequence server_connector_sequence_{};
    ::testing::Sequence client_connector_sequence_{};
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
