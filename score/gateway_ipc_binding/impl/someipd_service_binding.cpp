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

#include <string_view>
#include <utility>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/mw/com/types.h"
#include "score/mw/log/logging.h"
#include "service_binding.hpp"

namespace score::gateway_ipc_binding::mw_com {

namespace {

/// \brief Log prefix shared by the whole binding
constexpr std::string_view kLog_tag{"[gateway_ipc_binding]"};

}  // namespace

Someipd_service_provider::Someipd_service_provider(Someipd_service_skeleton skeleton) noexcept
    : m_skeleton{std::move(skeleton)} {}

Someipd_service_provider::~Someipd_service_provider() noexcept = default;

Result<std::unique_ptr<Someipd_service_provider>> Someipd_service_provider::create(
    std::string const& instance_specifier,
    Add_service_configuration_handler add_service_configuration_handler) noexcept {
    auto specifier = make_instance_specifier(instance_specifier);
    if (!specifier.has_value()) {
        return MakeUnexpected<std::unique_ptr<Someipd_service_provider>>(
            std::move(specifier).error());
    }

    auto skeleton = Someipd_service_skeleton::Create(specifier.value());
    if (!skeleton.has_value()) {
        score::mw::log::LogError() << kLog_tag << "Failed to create SomeipdService skeleton for"
                                   << instance_specifier << ":" << skeleton.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_someipd_service_creation_failed);
    }

    std::unique_ptr<Someipd_service_provider> provider{
        new Someipd_service_provider{std::move(skeleton).value()}};
    auto const registered = provider->m_skeleton.add_service_configuration.RegisterHandler(
        [handler = std::move(add_service_configuration_handler)](
            bool& result, Service_configuration const& configuration) {
            result = handler(configuration);
        });
    if (!registered.has_value()) {
        score::mw::log::LogError()
            << kLog_tag
            << "Failed to register AddServiceConfiguration handler:" << registered.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_someipd_service_creation_failed);
    }

    return provider;
}

Result<void> Someipd_service_provider::offer() noexcept {
    auto const offered = m_skeleton.OfferService();
    if (!offered.has_value()) {
        score::mw::log::LogError()
            << kLog_tag << "Failed to offer SomeipdService:" << offered.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_someipd_service_offer_failed);
    }
    return {};
}

Someipd_service_consumer::Someipd_service_consumer(
    Service_configuration const& service_configuration) noexcept
    : m_service_configuration{service_configuration} {}

Result<std::unique_ptr<Someipd_service_consumer>> Someipd_service_consumer::create(
    std::string const& instance_specifier,
    Service_configuration const& service_configuration) noexcept {
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
    std::unique_ptr<Someipd_service_consumer> consumer{
        new Someipd_service_consumer{service_configuration}};

    // The handler can already be invoked from within StartFindService, on this very thread. The
    // consumer is fully constructed at this point, so that is safe.
    auto find_handle = Someipd_service_proxy::StartFindService(
        [raw_consumer = consumer.get()](auto handles, auto) noexcept {
            raw_consumer->on_find_service(std::move(handles));
        },
        std::move(specifier).value());
    if (!find_handle.has_value()) {
        score::mw::log::LogError()
            << kLog_tag << "Failed to start service discovery for SomeipdService"
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
        auto const stopped = Someipd_service_proxy::StopFindService(find_handle.value());
        if (!stopped.has_value()) {
            score::mw::log::LogError()
                << kLog_tag
                << "Failed to stop service discovery for SomeipdService:" << stopped.error();
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
    std::optional<Someipd_service_proxy> outdated_proxy{};

    {
        std::lock_guard const lock{m_mutex};

        std::swap(outdated_proxy, m_proxy);
        m_connected.store(false);

        if (handles.empty()) {
            score::mw::log::LogInfo()
                << kLog_tag << "SomeipdService is gone, peer is not connected anymore";
            return;
        }

        // Only one someipd offers the instance. Should the deployment ever offer more, the
        // first handle is as good as any.
        auto proxy = Someipd_service_proxy::Create(handles.front());
        // TODO: Retry Create. mw::com can report an offer that is already gone and then does not
        // call this handler again for the same handle, so gatewayd stays disconnected.
        if (!proxy.has_value()) {
            score::mw::log::LogError()
                << kLog_tag << "Failed to create SomeipdService proxy:" << proxy.error();
            return;
        }

        // A restarted someipd needs the configuration again. Send it to every new proxy.
        // Destroy reply before moving proxy: reply's deleter references proxy's method instance.
        bool accepted{false};
        {
            auto const reply = proxy->add_service_configuration(m_service_configuration);
            accepted = reply.has_value() && *reply.value();
        }

        // Move proxy into m_proxy so ~Someipd_service_proxy() does not run while m_mutex is held.
        m_proxy.emplace(std::move(proxy).value());
        if (!accepted) {
            score::mw::log::LogError()
                << kLog_tag << "AddServiceConfiguration failed or was rejected";
            return;
        }
        m_connected.store(true);
    }

    score::mw::log::LogInfo() << kLog_tag << "SomeipdService found, peer is connected";
}

}  // namespace score::gateway_ipc_binding::mw_com
