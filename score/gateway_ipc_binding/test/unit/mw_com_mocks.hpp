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

#ifndef SCORE_GATEWAY_IPC_BINDING_TEST_UNIT_MW_COM_MOCKS_HPP
#define SCORE_GATEWAY_IPC_BINDING_TEST_UNIT_MW_COM_MOCKS_HPP

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <memory>
#include <optional>
#include <score/span.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "score/memory/data_type_size_info.h"
#include "score/mw/com/impl/bindings/mock_binding/sample_allocatee_ptr.h"
#include "score/mw/com/impl/bindings/mock_binding/sample_ptr.h"
#include "score/mw/com/impl/configuration/lola_event_instance_deployment.h"
#include "score/mw/com/impl/configuration/lola_service_instance_deployment.h"
#include "score/mw/com/impl/configuration/lola_service_instance_id.h"
#include "score/mw/com/impl/configuration/lola_service_type_deployment.h"
#include "score/mw/com/impl/configuration/quality_type.h"
#include "score/mw/com/impl/configuration/service_identifier_type.h"
#include "score/mw/com/impl/configuration/service_instance_deployment.h"
#include "score/mw/com/impl/configuration/service_instance_id.h"
#include "score/mw/com/impl/configuration/service_type_deployment.h"
#include "score/mw/com/impl/enriched_instance_identifier.h"
#include "score/mw/com/impl/find_service_handle.h"
#include "score/mw/com/impl/find_service_handler.h"
#include "score/mw/com/impl/generic_proxy_event_binding.h"
#include "score/mw/com/impl/generic_skeleton_event_binding.h"
#include "score/mw/com/impl/handle_type.h"
#include "score/mw/com/impl/i_binding_runtime.h"
#include "score/mw/com/impl/i_runtime.h"
#include "score/mw/com/impl/i_service_discovery.h"
#include "score/mw/com/impl/instance_identifier.h"
#include "score/mw/com/impl/instance_specifier.h"
#include "score/mw/com/impl/methods/proxy_method_binding.h"
#include "score/mw/com/impl/methods/skeleton_method_binding.h"
#include "score/mw/com/impl/plumbing/i_proxy_binding_factory.h"
#include "score/mw/com/impl/plumbing/i_proxy_event_binding_factory.h"
#include "score/mw/com/impl/plumbing/i_proxy_method_binding_factory.h"
#include "score/mw/com/impl/plumbing/i_skeleton_binding_factory.h"
#include "score/mw/com/impl/plumbing/i_skeleton_event_binding_factory.h"
#include "score/mw/com/impl/plumbing/i_skeleton_method_binding_factory.h"
#include "score/mw/com/impl/plumbing/proxy_binding_factory.h"
#include "score/mw/com/impl/plumbing/proxy_event_binding_factory.h"
#include "score/mw/com/impl/plumbing/proxy_method_binding_factory.h"
#include "score/mw/com/impl/plumbing/sample_allocatee_ptr.h"
#include "score/mw/com/impl/plumbing/sample_ptr.h"
#include "score/mw/com/impl/plumbing/skeleton_binding_factory.h"
#include "score/mw/com/impl/plumbing/skeleton_event_binding_factory.h"
#include "score/mw/com/impl/plumbing/skeleton_method_binding_factory.h"
#include "score/mw/com/impl/proxy_binding.h"
#include "score/mw/com/impl/runtime.h"
#include "score/mw/com/impl/scoped_event_receive_handler.h"
#include "score/mw/com/impl/skeleton_binding.h"
#include "score/mw/com/impl/subscription_state.h"
#include "score/mw/com/impl/tracing/configuration/i_tracing_filter_config.h"
#include "score/result/result.h"

namespace score::gateway_ipc_binding::testing {

class RuntimeMock : public score::mw::com::impl::IRuntime {
   public:
    RuntimeMock();
    ~RuntimeMock() override;

    RuntimeMock(RuntimeMock const&) = delete;
    RuntimeMock& operator=(RuntimeMock const&) = delete;
    RuntimeMock(RuntimeMock&&) = delete;
    RuntimeMock& operator=(RuntimeMock&&) = delete;

    MOCK_METHOD(score::mw::com::impl::IBindingRuntime*, GetBindingRuntime,
                (score::mw::com::impl::BindingType), (const, noexcept, override));
    MOCK_METHOD(std::vector<score::mw::com::impl::InstanceIdentifier>, resolve,
                (score::mw::com::impl::InstanceSpecifier const&), (const, override));
    MOCK_METHOD(score::mw::com::impl::IServiceDiscovery&, GetServiceDiscovery, (),
                (ref(&), noexcept, override));
    MOCK_METHOD(score::mw::com::impl::tracing::ITracingFilterConfig const*, GetTracingFilterConfig,
                (), (const, override));
    MOCK_METHOD(score::mw::com::impl::tracing::ITracingRuntime*, GetTracingRuntime, (),
                (const, noexcept, override));
};

class RuntimeMockGuard {
   public:
    RuntimeMockGuard() noexcept { score::mw::com::impl::Runtime::InjectMock(&runtime_mock_); }
    ~RuntimeMockGuard() noexcept { score::mw::com::impl::Runtime::InjectMock(nullptr); }

    RuntimeMockGuard(RuntimeMockGuard const&) = delete;
    RuntimeMockGuard& operator=(RuntimeMockGuard const&) = delete;
    RuntimeMockGuard(RuntimeMockGuard&&) = delete;
    RuntimeMockGuard& operator=(RuntimeMockGuard&&) = delete;

    ::testing::NiceMock<RuntimeMock>& mock() noexcept { return runtime_mock_; }
    ::testing::NiceMock<RuntimeMock> const& mock() const noexcept { return runtime_mock_; }

   private:
    ::testing::NiceMock<RuntimeMock> runtime_mock_{};
};

class ServiceDiscoveryMock : public score::mw::com::impl::IServiceDiscovery {
   public:
    ServiceDiscoveryMock();
    ~ServiceDiscoveryMock() override;

    ServiceDiscoveryMock(ServiceDiscoveryMock const&) = delete;
    ServiceDiscoveryMock& operator=(ServiceDiscoveryMock const&) = delete;
    ServiceDiscoveryMock(ServiceDiscoveryMock&&) = delete;
    ServiceDiscoveryMock& operator=(ServiceDiscoveryMock&&) = delete;

    MOCK_METHOD(score::Result<void>, OfferService, (score::mw::com::impl::InstanceIdentifier),
                (override));
    MOCK_METHOD(score::Result<void>, StopOfferService, (score::mw::com::impl::InstanceIdentifier),
                (override));
    MOCK_METHOD(score::Result<void>, StopOfferService,
                (score::mw::com::impl::InstanceIdentifier,
                 score::mw::com::impl::IServiceDiscovery::QualityTypeSelector),
                (override));
    MOCK_METHOD(score::Result<score::mw::com::impl::FindServiceHandle>, StartFindService,
                (score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>,
                 score::mw::com::impl::InstanceSpecifier),
                (override));
    MOCK_METHOD(score::Result<score::mw::com::impl::FindServiceHandle>, StartFindService,
                (score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>,
                 score::mw::com::impl::InstanceIdentifier),
                (override));
    MOCK_METHOD(score::Result<score::mw::com::impl::FindServiceHandle>, StartFindService,
                (score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>,
                 score::mw::com::impl::EnrichedInstanceIdentifier),
                (override));
    MOCK_METHOD(score::Result<void>, StopFindService, (score::mw::com::impl::FindServiceHandle),
                (override));
    MOCK_METHOD(score::Result<
                    score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType>>,
                FindService, (score::mw::com::impl::InstanceIdentifier), (override));
    MOCK_METHOD(score::Result<
                    score::mw::com::impl::ServiceHandleContainer<score::mw::com::impl::HandleType>>,
                FindService, (score::mw::com::impl::InstanceSpecifier), (override));
};

namespace mock_binding {

class Proxy : public score::mw::com::impl::ProxyBinding {
   public:
    Proxy();
    ~Proxy() override;

    Proxy(Proxy const&) = delete;
    Proxy& operator=(Proxy const&) = delete;
    Proxy(Proxy&&) = delete;
    Proxy& operator=(Proxy&&) = delete;

    MOCK_METHOD(bool, IsEventProvided, (std::string_view), (const, override));
    MOCK_METHOD(score::Result<void>, SetupMethods, (std::size_t), (override));
    MOCK_METHOD(void, PrepareDeinitialize, (), (override));
    MOCK_METHOD(void, FinalizeDeinitialize, (), (override));
};

class GenericProxyEvent : public score::mw::com::impl::GenericProxyEventBinding {
   public:
    GenericProxyEvent();
    ~GenericProxyEvent() override;

    GenericProxyEvent(GenericProxyEvent const&) = delete;
    GenericProxyEvent& operator=(GenericProxyEvent const&) = delete;
    GenericProxyEvent(GenericProxyEvent&&) = delete;
    GenericProxyEvent& operator=(GenericProxyEvent&&) = delete;

    MOCK_METHOD(score::mw::com::impl::SubscriptionState, GetSubscriptionState, (),
                (const, noexcept, override));
    MOCK_METHOD(void, Unsubscribe, (), (noexcept, override));
    MOCK_METHOD(score::Result<void>, Subscribe, (std::size_t), (noexcept, override));
    MOCK_METHOD(score::Result<std::size_t>, GetNumNewSamplesAvailable, (), (const, override));
    MOCK_METHOD(score::memory::DataTypeSizeInfo, GetDataTypeSizeInfo, (), (const, override));
    MOCK_METHOD(bool, HasSerializedFormat, (), (const, noexcept, override));
    MOCK_METHOD(score::Result<std::size_t>, GetNewSamples,
                (typename score::mw::com::impl::GenericProxyEventBinding::Callback&&,
                 score::mw::com::impl::TrackerGuardFactory&),
                (override));
    MOCK_METHOD(score::Result<void>, SetReceiveHandler,
                (std::weak_ptr<score::mw::com::impl::ScopedEventReceiveHandler>),
                (noexcept, override));
    MOCK_METHOD(score::Result<void>, UnsetReceiveHandler, (), (noexcept, override));
    MOCK_METHOD(score::Result<void>, SetSubscriptionStateChangeHandler,
                (score::mw::com::impl::SubscriptionStateChangeHandler), (noexcept, override));
    MOCK_METHOD(score::Result<void>, UnsetSubscriptionStateChangeHandler, (), (noexcept, override));
    MOCK_METHOD(std::optional<std::uint16_t>, GetMaxSampleCount, (), (const, noexcept, override));
    MOCK_METHOD(score::mw::com::impl::BindingType, GetBindingType, (), (const, noexcept, override));
};

class ProxyMethod : public score::mw::com::impl::ProxyMethodBinding {
   public:
    ProxyMethod();
    ~ProxyMethod() override;

    ProxyMethod(ProxyMethod const&) = delete;
    ProxyMethod& operator=(ProxyMethod const&) = delete;
    ProxyMethod(ProxyMethod&&) = delete;
    ProxyMethod& operator=(ProxyMethod&&) = delete;

    MOCK_METHOD(score::Result<score::cpp::span<std::byte>>, GetInArgsBuffer, (std::size_t),
                (override));
    MOCK_METHOD(score::Result<score::cpp::span<std::byte>>, GetReturnValueBuffer, (std::size_t),
                (override));
    MOCK_METHOD(score::Result<void>, DoCall, (std::size_t), (override));
};

class Skeleton : public score::mw::com::impl::SkeletonBinding {
   public:
    Skeleton();
    ~Skeleton() override;

    Skeleton(Skeleton const&) = delete;
    Skeleton& operator=(Skeleton const&) = delete;
    Skeleton(Skeleton&&) = delete;
    Skeleton& operator=(Skeleton&&) = delete;

    MOCK_METHOD(score::Result<void>, PrepareOffer,
                (SkeletonEventBindings&, SkeletonFieldBindings&,
                 std::optional<RegisterShmObjectTraceCallback>),
                (override));
    MOCK_METHOD(void, PrepareStopOffer, (std::optional<UnregisterShmObjectTraceCallback>),
                (override));
    MOCK_METHOD(score::mw::com::impl::BindingType, GetBindingType, (), (const, noexcept, override));
    MOCK_METHOD(bool, VerifyAllMethodHandlersRegistered, (), (const, override));
};

class GenericSkeletonEvent : public score::mw::com::impl::GenericSkeletonEventBinding {
   public:
    GenericSkeletonEvent();
    ~GenericSkeletonEvent() override;

    GenericSkeletonEvent(GenericSkeletonEvent const&) = delete;
    GenericSkeletonEvent& operator=(GenericSkeletonEvent const&) = delete;
    GenericSkeletonEvent(GenericSkeletonEvent&&) = delete;
    GenericSkeletonEvent& operator=(GenericSkeletonEvent&&) = delete;

    MOCK_METHOD(score::Result<void>, Send,
                (score::mw::com::impl::SampleAllocateePtr<void> sample,
                 std::optional<SendTraceCallback>),
                (override));
    MOCK_METHOD(score::Result<score::mw::com::impl::SampleAllocateePtr<void>>, Allocate,
                (score::mw::com::impl::SampleAllocateeGuard), (override));
    MOCK_METHOD(score::Result<score::mw::com::impl::SamplePtr<void>>, GetLatestSample,
                (score::mw::com::impl::QualityType), (override));
    MOCK_METHOD(score::Result<void>, PrepareOffer,
                (std::optional<score::mw::com::impl::InitializeSampleCallback> const&), (override));
    MOCK_METHOD(void, PrepareStopOffer, (), (override));
    MOCK_METHOD(score::memory::DataTypeSizeInfo, GetEventDataTypeSizeInfo, (), (const, override));
    MOCK_METHOD(score::mw::com::impl::BindingType, GetBindingType, (), (const, noexcept, override));
    MOCK_METHOD(void, SetSkeletonEventTracingData,
                (score::mw::com::impl::tracing::SkeletonEventTracingData), (noexcept, override));
    MOCK_METHOD(score::Result<void>, Notify, (), (override));
    MOCK_METHOD(score::Result<void>, SetReceiveHandlerRegistrationChangedHandler,
                (score::mw::com::impl::ReceiveHandlerRegistrationChangedCallback),
                (noexcept, override));
    MOCK_METHOD(score::Result<void>, UnsetReceiveHandlerRegistrationChangedHandler, (),
                (noexcept, override));
};

class SkeletonMethod : public score::mw::com::impl::SkeletonMethodBinding {
   public:
    SkeletonMethod();
    ~SkeletonMethod() override;

    SkeletonMethod(SkeletonMethod const&) = delete;
    SkeletonMethod& operator=(SkeletonMethod const&) = delete;
    SkeletonMethod(SkeletonMethod&&) = delete;
    SkeletonMethod& operator=(SkeletonMethod&&) = delete;

    MOCK_METHOD(score::Result<void>, RegisterHandler, (TypeErasedHandler&&), (override));
};

}  // namespace mock_binding

using Proxy = mock_binding::Proxy;
using GenericProxyEvent = mock_binding::GenericProxyEvent;
using ProxyMethod = mock_binding::ProxyMethod;
using Skeleton = mock_binding::Skeleton;
using GenericSkeletonEvent = mock_binding::GenericSkeletonEvent;
using SkeletonMethod = mock_binding::SkeletonMethod;

template <typename Factory, typename Mock>
class FactoryMockGuard {
   public:
    FactoryMockGuard() noexcept { Factory::InjectMockBinding(&factory_mock_); }
    ~FactoryMockGuard() noexcept { Factory::InjectMockBinding(nullptr); }

    FactoryMockGuard(FactoryMockGuard const&) = delete;
    FactoryMockGuard& operator=(FactoryMockGuard const&) = delete;
    FactoryMockGuard(FactoryMockGuard&&) = delete;
    FactoryMockGuard& operator=(FactoryMockGuard&&) = delete;

    ::testing::NiceMock<Mock>& mock() noexcept { return factory_mock_; }
    ::testing::NiceMock<Mock> const& mock() const noexcept { return factory_mock_; }

   private:
    ::testing::NiceMock<Mock> factory_mock_{};
};

class ProxyBindingFactoryMock : public score::mw::com::impl::IProxyBindingFactory {
   public:
    ProxyBindingFactoryMock();
    ~ProxyBindingFactoryMock() override;

    ProxyBindingFactoryMock(ProxyBindingFactoryMock const&) = delete;
    ProxyBindingFactoryMock& operator=(ProxyBindingFactoryMock const&) = delete;
    ProxyBindingFactoryMock(ProxyBindingFactoryMock&&) = delete;
    ProxyBindingFactoryMock& operator=(ProxyBindingFactoryMock&&) = delete;

    MOCK_METHOD(score::Result<std::unique_ptr<score::mw::com::impl::ProxyBinding>>, Create,
                (score::mw::com::impl::HandleType const&), (noexcept, override));
};

using ProxyBindingFactoryMockGuard =
    FactoryMockGuard<score::mw::com::impl::ProxyBindingFactory, ProxyBindingFactoryMock>;

class GenericProxyEventBindingFactoryMock
    : public score::mw::com::impl::IGenericProxyEventBindingFactory {
   public:
    GenericProxyEventBindingFactoryMock();
    ~GenericProxyEventBindingFactoryMock() override;

    GenericProxyEventBindingFactoryMock(GenericProxyEventBindingFactoryMock const&) = delete;
    GenericProxyEventBindingFactoryMock& operator=(GenericProxyEventBindingFactoryMock const&) =
        delete;
    GenericProxyEventBindingFactoryMock(GenericProxyEventBindingFactoryMock&&) = delete;
    GenericProxyEventBindingFactoryMock& operator=(GenericProxyEventBindingFactoryMock&&) = delete;

    MOCK_METHOD(score::Result<std::unique_ptr<score::mw::com::impl::GenericProxyEventBinding>>,
                Create,
                (score::mw::com::impl::HandleType, score::mw::com::impl::ProxyBinding&,
                 std::string_view event_name,
                 score::mw::com::impl::ServiceElementType service_element_type),
                (override));
};

using GenericProxyEventBindingFactoryMockGuard =
    FactoryMockGuard<score::mw::com::impl::GenericProxyEventBindingFactory,
                     GenericProxyEventBindingFactoryMock>;

class ProxyMethodBindingFactoryMock : public score::mw::com::impl::IProxyMethodBindingFactory {
   public:
    ProxyMethodBindingFactoryMock();
    ~ProxyMethodBindingFactoryMock() override;

    ProxyMethodBindingFactoryMock(ProxyMethodBindingFactoryMock const&) = delete;
    ProxyMethodBindingFactoryMock& operator=(ProxyMethodBindingFactoryMock const&) = delete;
    ProxyMethodBindingFactoryMock(ProxyMethodBindingFactoryMock&&) = delete;
    ProxyMethodBindingFactoryMock& operator=(ProxyMethodBindingFactoryMock&&) = delete;

    MOCK_METHOD(score::Result<std::unique_ptr<score::mw::com::impl::ProxyMethodBinding>>, Create,
                (score::mw::com::impl::HandleType parent_handle,
                 score::mw::com::impl::ProxyBinding& parent_binding, std::string_view method_name,
                 score::mw::com::impl::MethodType method_type,
                 std::optional<score::memory::DataTypeSizeInfo> in_arg_type_info,
                 std::optional<score::memory::DataTypeSizeInfo> return_type_info),
                (override));
};

using ProxyMethodBindingFactoryMockGuard =
    FactoryMockGuard<score::mw::com::impl::ProxyMethodBindingFactory,
                     ProxyMethodBindingFactoryMock>;

class SkeletonBindingFactoryMock : public score::mw::com::impl::ISkeletonBindingFactory {
   public:
    SkeletonBindingFactoryMock();
    ~SkeletonBindingFactoryMock() override;

    SkeletonBindingFactoryMock(SkeletonBindingFactoryMock const&) = delete;
    SkeletonBindingFactoryMock& operator=(SkeletonBindingFactoryMock const&) = delete;
    SkeletonBindingFactoryMock(SkeletonBindingFactoryMock&&) = delete;
    SkeletonBindingFactoryMock& operator=(SkeletonBindingFactoryMock&&) = delete;

    MOCK_METHOD(std::unique_ptr<score::mw::com::impl::SkeletonBinding>, Create,
                (score::mw::com::impl::InstanceIdentifier const&), (noexcept, override));
};

using SkeletonBindingFactoryMockGuard =
    FactoryMockGuard<score::mw::com::impl::SkeletonBindingFactory, SkeletonBindingFactoryMock>;

class SkeletonEventBindingFactoryMock : public score::mw::com::impl::ISkeletonEventBindingFactory {
   public:
    SkeletonEventBindingFactoryMock();
    ~SkeletonEventBindingFactoryMock() override;

    SkeletonEventBindingFactoryMock(SkeletonEventBindingFactoryMock const&) = delete;
    SkeletonEventBindingFactoryMock& operator=(SkeletonEventBindingFactoryMock const&) = delete;
    SkeletonEventBindingFactoryMock(SkeletonEventBindingFactoryMock&&) = delete;
    SkeletonEventBindingFactoryMock& operator=(SkeletonEventBindingFactoryMock&&) = delete;

    MOCK_METHOD(std::unique_ptr<score::mw::com::impl::SkeletonEventBinding>, Create,
                (score::mw::com::impl::InstanceIdentifier const&,
                 score::mw::com::impl::SkeletonBinding&, std::string_view,
                 score::memory::DataTypeSizeInfo),
                (override));
};

using SkeletonEventBindingFactoryMockGuard =
    FactoryMockGuard<score::mw::com::impl::SkeletonEventBindingFactory,
                     SkeletonEventBindingFactoryMock>;

class SkeletonMethodBindingFactoryMock
    : public score::mw::com::impl::ISkeletonMethodBindingFactory {
   public:
    SkeletonMethodBindingFactoryMock();
    ~SkeletonMethodBindingFactoryMock() override;

    SkeletonMethodBindingFactoryMock(SkeletonMethodBindingFactoryMock const&) = delete;
    SkeletonMethodBindingFactoryMock& operator=(SkeletonMethodBindingFactoryMock const&) = delete;
    SkeletonMethodBindingFactoryMock(SkeletonMethodBindingFactoryMock&&) = delete;
    SkeletonMethodBindingFactoryMock& operator=(SkeletonMethodBindingFactoryMock&&) = delete;

    MOCK_METHOD(std::unique_ptr<score::mw::com::impl::SkeletonMethodBinding>, Create,
                (score::mw::com::impl::InstanceIdentifier const&,
                 score::mw::com::impl::SkeletonBinding&, std::string_view,
                 score::mw::com::impl::MethodType, std::optional<score::memory::DataTypeSizeInfo>,
                 std::optional<score::memory::DataTypeSizeInfo>),
                (override));
};

using SkeletonMethodBindingFactoryMockGuard =
    FactoryMockGuard<score::mw::com::impl::SkeletonMethodBindingFactory,
                     SkeletonMethodBindingFactoryMock>;

inline score::mw::com::impl::SamplePtr<void> MakeMockSample(
    void* const raw_ptr, score::mw::com::impl::TrackerGuardFactory& tracker) {
    return score::mw::com::impl::SamplePtr<void>{
        score::mw::com::impl::mock_binding::SamplePtr{raw_ptr, +[](void*) noexcept {}},
        *tracker.TakeGuard()};
}

inline score::Result<score::mw::com::impl::SampleAllocateePtr<void>> MakeAllocatedSample(
    void* const buffer, score::mw::com::impl::SampleAllocateeGuard guard) noexcept {
    score::mw::com::impl::mock_binding::SampleAllocateePtr mock_ptr{buffer, +[](void*) noexcept {}};
    return score::mw::com::impl::MakeSampleAllocateePtr(std::move(mock_ptr), std::move(guard));
}

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
    ~DeployedInstanceHolder() = default;

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

}  // namespace score::gateway_ipc_binding::testing

namespace score::mw::com::impl::mock_binding {

using Proxy = score::gateway_ipc_binding::testing::mock_binding::Proxy;
using GenericProxyEvent = score::gateway_ipc_binding::testing::mock_binding::GenericProxyEvent;
using ProxyMethod = score::gateway_ipc_binding::testing::mock_binding::ProxyMethod;
using Skeleton = score::gateway_ipc_binding::testing::mock_binding::Skeleton;
using GenericSkeletonEvent =
    score::gateway_ipc_binding::testing::mock_binding::GenericSkeletonEvent;
using SkeletonMethod = score::gateway_ipc_binding::testing::mock_binding::SkeletonMethod;

}  // namespace score::mw::com::impl::mock_binding

#endif  // SCORE_GATEWAY_IPC_BINDING_TEST_UNIT_MW_COM_MOCKS_HPP
