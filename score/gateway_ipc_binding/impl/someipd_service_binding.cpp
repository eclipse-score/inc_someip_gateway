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
    if (!add_service_configuration_handler) {
        score::mw::log::LogError()
            << kLog_tag << "Cannot create SomeipdService provider with an empty handler";
        return MakeUnexpected(Mw_com_binding_error::logic_error_invalid_service_configuration);
    }

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
    std::unique_ptr<Service_configuration const> service_configuration) noexcept
    : m_service_configuration{std::move(service_configuration)} {}

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

    auto owned_configuration = std::make_unique<Service_configuration const>(service_configuration);

    // Not make_unique: the constructor is private.
    std::unique_ptr<Someipd_service_consumer> consumer{
        new Someipd_service_consumer{std::move(owned_configuration)}};

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
    // Stop discovery first, so that no new handler can start while the proxy is destroyed below.
    std::optional<score::mw::com::FindServiceHandle> find_handle{};
    {
        std::lock_guard const lock{m_mutex};
        m_stopping = true;
        ++m_discovery_epoch;
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

    std::optional<Someipd_service_proxy> proxy_to_destroy{};
    {
        std::lock_guard const lock{m_mutex};
        m_active_handle.reset();
        m_connected.store(false);
        std::swap(proxy_to_destroy, m_proxy);
    }
    proxy_to_destroy.reset();
}

bool Someipd_service_consumer::is_connected() const noexcept { return m_connected.load(); }

void Someipd_service_consumer::on_find_service(
    score::mw::com::ServiceHandleContainer<score::mw::com::HandleType> handles) noexcept {
    std::optional<Someipd_service_proxy> outdated_proxy{};

    if (handles.empty()) {
        {
            std::lock_guard const lock{m_mutex};
            ++m_discovery_epoch;
            m_active_handle.reset();
            m_connected.store(false);
            std::swap(outdated_proxy, m_proxy);
        }
        outdated_proxy.reset();
        score::mw::log::LogInfo() << kLog_tag
                                  << "SomeipdService is gone, peer is not connected anymore";
        return;
    }

    auto const target_handle = handles.front();
    std::uint64_t expected_epoch{0U};
    {
        std::lock_guard const lock{m_mutex};
        if (m_stopping) {
            return;
        }
        if (m_connected.load() && m_active_handle.has_value() &&
            (*m_active_handle == target_handle)) {
            return;
        }
        expected_epoch = ++m_discovery_epoch;
        m_active_handle.reset();
        m_connected.store(false);
        std::swap(outdated_proxy, m_proxy);
    }
    // Proxy construction, destruction, and synchronous RPC calls must run outside m_mutex to avoid
    // inverting lock order with mw::com's service-discovery lock.
    outdated_proxy.reset();

    auto created = Someipd_service_proxy::Create(target_handle);
    if (!created.has_value()) {
        score::mw::log::LogError()
            << kLog_tag << "Failed to create SomeipdService proxy:" << created.error();
        return;
    }
    std::optional<Someipd_service_proxy> candidate_proxy{std::move(created).value()};

    bool superseded{false};
    {
        std::lock_guard const lock{m_mutex};
        superseded = m_stopping || (m_discovery_epoch != expected_epoch);
    }
    if (superseded) {
        candidate_proxy.reset();
        return;
    }

    // Destroy reply before moving or resetting candidate_proxy: reply's deleter references the
    // proxy's method instance.
    bool accepted{false};
    {
        auto const reply = candidate_proxy->add_service_configuration(*m_service_configuration);
        accepted = reply.has_value() && *reply.value();
    }

    if (!accepted) {
        candidate_proxy.reset();
        score::mw::log::LogError() << kLog_tag << "AddServiceConfiguration failed or was rejected";
        return;
    }

    bool connected_now{false};
    {
        std::lock_guard const lock{m_mutex};
        if (!m_stopping && (m_discovery_epoch == expected_epoch)) {
            std::swap(m_proxy, candidate_proxy);
            m_active_handle.emplace(target_handle);
            m_connected.store(true);
            connected_now = true;
        }
    }
    candidate_proxy.reset();

    if (connected_now) {
        score::mw::log::LogInfo() << kLog_tag << "SomeipdService found, peer is connected";
    }
}

}  // namespace score::gateway_ipc_binding::mw_com
