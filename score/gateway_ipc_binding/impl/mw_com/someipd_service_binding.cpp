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

#include "someipd_service_binding.hpp"

#include <utility>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/mw/com/runtime.h"
#include "score/mw/com/types.h"
#include "score/mw/log/logging.h"

namespace score::gateway_ipc_binding::mw_com {

namespace {

/// \brief Turn a configured string into a mw::com InstanceSpecifier
Result<score::mw::com::InstanceSpecifier> make_instance_specifier(
    std::string const& instance_specifier) noexcept {
    auto specifier = score::mw::com::InstanceSpecifier::Create(std::string{instance_specifier});
    if (!specifier.has_value()) {
        score::mw::log::LogError()
            << "[gateway_ipc_binding] Invalid SomeipdService instance specifier"
            << instance_specifier << ":" << specifier.error();
        return MakeUnexpected(Mw_com_binding_error::logic_error_invalid_instance_specifier);
    }
    return std::move(specifier).value();
}

/// \brief Check that the deployment actually knows the instance
/// \details `StartFindService` accepts a well-formed specifier that resolves to no instance at all
///          and then simply never reports anything. Resolving up front turns that silent
///          misconfiguration into an error at construction time, symmetric to the provider side,
///          where `Create` already fails for an unknown instance.
Result<void> check_instance_is_deployed(score::mw::com::InstanceSpecifier const& specifier,
                                        std::string const& instance_specifier) noexcept {
    auto const identifiers = score::mw::com::runtime::ResolveInstanceIDs(specifier);
    if (!identifiers.has_value()) {
        score::mw::log::LogError()
            << "[gateway_ipc_binding] Failed to resolve SomeipdService instance specifier"
            << instance_specifier << ":" << identifiers.error();
        return MakeUnexpected(Mw_com_binding_error::logic_error_invalid_instance_specifier);
    }
    if (identifiers.value().empty()) {
        score::mw::log::LogError() << "[gateway_ipc_binding] SomeipdService instance specifier"
                                   << instance_specifier << "is not part of the mw::com deployment";
        return MakeUnexpected(Mw_com_binding_error::logic_error_invalid_instance_specifier);
    }
    return {};
}

}  // namespace

Someipd_service_provider::Someipd_service_provider(
    someip::Someipd_service_skeleton skeleton) noexcept
    : m_skeleton{std::move(skeleton)} {}

Someipd_service_provider::~Someipd_service_provider() noexcept = default;

Result<std::unique_ptr<Someipd_service_provider>> Someipd_service_provider::create(
    std::string const& instance_specifier) noexcept {
    auto specifier = make_instance_specifier(instance_specifier);
    if (!specifier.has_value()) {
        return MakeUnexpected<std::unique_ptr<Someipd_service_provider>>(
            std::move(specifier).error());
    }

    auto skeleton = someip::Someipd_service_skeleton::Create(specifier.value());
    if (!skeleton.has_value()) {
        score::mw::log::LogError()
            << "[gateway_ipc_binding] Failed to create SomeipdService skeleton for"
            << instance_specifier << ":" << skeleton.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_someipd_service_creation_failed);
    }

    return std::unique_ptr<Someipd_service_provider>{
        new Someipd_service_provider{std::move(skeleton).value()}};
}

Result<void> Someipd_service_provider::offer() noexcept {
    auto const offered = m_skeleton.OfferService();
    if (!offered.has_value()) {
        score::mw::log::LogError()
            << "[gateway_ipc_binding] Failed to offer SomeipdService:" << offered.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_someipd_service_offer_failed);
    }
    return {};
}

Result<std::unique_ptr<Someipd_service_consumer>> Someipd_service_consumer::create(
    std::string const& instance_specifier) noexcept {
    auto specifier = make_instance_specifier(instance_specifier);
    if (!specifier.has_value()) {
        return MakeUnexpected<std::unique_ptr<Someipd_service_consumer>>(
            std::move(specifier).error());
    }

    auto deployed = check_instance_is_deployed(specifier.value(), instance_specifier);
    if (!deployed.has_value()) {
        return MakeUnexpected<std::unique_ptr<Someipd_service_consumer>>(
            std::move(deployed).error());
    }

    // Not make_unique: the constructor is private.
    std::unique_ptr<Someipd_service_consumer> consumer{new Someipd_service_consumer{}};

    // The handler can already be invoked from within StartFindService, on this very thread. The
    // consumer is fully constructed at this point, so that is safe.
    auto find_handle = someip::Someipd_service_proxy::StartFindService(
        [raw_consumer = consumer.get()](auto handles, auto) noexcept {
            raw_consumer->on_find_service(std::move(handles));
        },
        std::move(specifier).value());
    if (!find_handle.has_value()) {
        score::mw::log::LogError()
            << "[gateway_ipc_binding] Failed to start service discovery for SomeipdService"
            << instance_specifier << ":" << find_handle.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_someipd_service_find_failed);
    }

    std::lock_guard const lock{consumer->m_mutex};
    consumer->m_find_handle = std::move(find_handle).value();
    return consumer;
}

Someipd_service_consumer::~Someipd_service_consumer() noexcept {
    // Stop discovery first, so that no handler can run while the proxy is destroyed below.
    std::optional<score::mw::com::FindServiceHandle> find_handle{};
    {
        std::lock_guard const lock{m_mutex};
        std::swap(find_handle, m_find_handle);
    }
    if (find_handle.has_value()) {
        auto const stopped = someip::Someipd_service_proxy::StopFindService(find_handle.value());
        if (!stopped.has_value()) {
            score::mw::log::LogError()
                << "[gateway_ipc_binding] Failed to stop service discovery for SomeipdService:"
                << stopped.error();
        }
    }
    m_connected.store(false);
    m_proxy.reset();
}

bool Someipd_service_consumer::is_connected() const noexcept { return m_connected.load(); }

void Someipd_service_consumer::on_find_service(
    score::mw::com::ServiceHandleContainer<score::mw::com::HandleType> handles) noexcept {
    // Destroyed after the lock is released: the proxy destructor must not run while the mutex is
    // held, since it synchronises with mw::com internals that can call back into discovery.
    std::optional<someip::Someipd_service_proxy> outdated_proxy{};

    {
        std::lock_guard const lock{m_mutex};

        std::swap(outdated_proxy, m_proxy);
        m_connected.store(false);

        if (handles.empty()) {
            score::mw::log::LogInfo() << "[gateway_ipc_binding] SomeipdService is gone, peer is "
                                         "not connected anymore";
            return;
        }

        // maxSubscribers is 1 per instance, so there is exactly one peer daemon. Should the
        // deployment ever offer more, the first handle is as good as any.
        auto proxy = someip::Someipd_service_proxy::Create(handles.front());
        if (!proxy.has_value()) {
            score::mw::log::LogError()
                << "[gateway_ipc_binding] Failed to create SomeipdService proxy:" << proxy.error();
            return;
        }

        m_proxy.emplace(std::move(proxy).value());
        m_connected.store(true);
    }

    score::mw::log::LogInfo() << "[gateway_ipc_binding] SomeipdService found, peer is connected";
}

}  // namespace score::gateway_ipc_binding::mw_com
