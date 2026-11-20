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

#include "consumer_service_binding.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include "sample_layout.hpp"
#include "score/gateway_ipc_binding/error.hpp"
#include "score/mw/log/logging.h"
#include "score/socom/payload.hpp"

namespace score::gateway_ipc_binding::mw_com {

namespace {

/// \brief Log prefix shared by the whole binding
constexpr std::string_view kLog_tag{"[gateway_ipc_binding]"};

/// \brief Nesting depth of active `on_samples_available()` calls on the current thread across all
///        `Consumer_service_binding` instances, matching LoLa's process-wide receive-handler flag.
thread_local std::size_t g_receive_handler_depth{0U};

}  // namespace

Consumer_service_binding::Consumer_service_binding(
    std::vector<Event_config> const& events, std::size_t const max_sample_count,
    std::string instance_specifier, score::mw::com::InstanceSpecifier specifier) noexcept
    : m_instance_specifier{std::move(instance_specifier)}, m_specifier{std::move(specifier)} {
    for (auto const& event : events) {
        m_events.emplace_back(event.name, event.max_payload_size, max_sample_count);
    }
}

Consumer_service_binding::~Consumer_service_binding() noexcept {
    // See score/gateway_ipc_binding/docs/architecture/index.rst, section "Teardown". Stop
    // discovery first, so that no handler can race with the teardown below.
    std::optional<score::mw::com::FindServiceHandle> find_handle{};
    {
        std::lock_guard const lock{m_mutex};
        m_stopping = true;
        ++m_discovery_epoch;
        std::swap(find_handle, m_find_handle);
    }
    if (find_handle.has_value()) {
        auto const stopped = score::mw::com::GenericProxy::StopFindService(find_handle.value());
        if (!stopped.has_value()) {
            score::mw::log::LogError() << kLog_tag << "Failed to stop service discovery for"
                                       << m_instance_specifier << ":" << stopped.error();
        }
    }

    detach_from_peer();
    m_reconcile_worker.Shutdown();

    // The disabled connector never calls back, so it can be dropped last.
    m_disabled_connector.reset();
}

Result<std::unique_ptr<Consumer_service_binding>> Consumer_service_binding::create(
    score::socom::Runtime& runtime, Service_config const& config) noexcept {
    auto specifier = make_instance_specifier(config.instance_specifier);
    if (!specifier.has_value()) {
        return MakeUnexpected<std::unique_ptr<Consumer_service_binding>>(
            std::move(specifier).error());
    }

    auto deployed = check_instance_is_deployed(specifier.value(), config.instance_specifier);
    if (!deployed.has_value()) {
        return MakeUnexpected<std::unique_ptr<Consumer_service_binding>>(
            std::move(deployed).error());
    }

    // Not make_unique: the constructor is private.
    std::unique_ptr<Consumer_service_binding> binding{
        new Consumer_service_binding{config.events, config.max_sample_count,
                                     config.instance_specifier, std::move(specifier).value()}};

    auto created = binding->create_server_connector(runtime, config);
    if (!created.has_value()) {
        return MakeUnexpected<std::unique_ptr<Consumer_service_binding>>(
            std::move(created).error());
    }

    return binding;
}

Result<void> Consumer_service_binding::create_server_connector(
    score::socom::Runtime& runtime, Service_config const& config) noexcept {
    score::socom::Disabled_server_connector::Callbacks callbacks{
        [](score::socom::Enabled_server_connector&, score::socom::Method_id, score::socom::Payload,
           score::socom::Method_call_reply_data_opt) -> score::socom::Method_invocation::Uptr {
            // GenericProxy and GenericSkeleton are event only, so bridged methods are not
            // representable. Known gap, see score/gateway_ipc_binding/docs/architecture/index.rst.
            return nullptr;
        },
        [this](score::socom::Enabled_server_connector&, score::socom::Event_id const event_id,
               score::socom::Event_state const state) {
            on_event_subscription_change(event_id, state == score::socom::Event_state::subscribed);
        },
        [](score::socom::Enabled_server_connector&, score::socom::Event_id const event_id) {
            // LoLa has no pull API on the proxy side. Known gap, see
            // score/gateway_ipc_binding/docs/architecture/index.rst.
            score::mw::log::LogWarn()
                << kLog_tag << "Ignoring the update request for event" << event_id
                << ": mw::com cannot serve requested event updates";
        },
        [](score::socom::Enabled_server_connector&,
           score::socom::Method_id) -> Result<score::socom::Writable_payload> {
            return score::socom::Writable_payload{score::socom::Writable_payload::Writable_span{}};
        }};

    score::socom::Server_service_interface_definition const definition{
        config.interface, score::socom::to_num_of_methods(0U),
        score::socom::to_num_of_events(config.events.size())};

    auto connector =
        runtime.make_server_connector(definition, config.instance, std::move(callbacks));
    if (!connector.has_value()) {
        score::mw::log::LogError() << kLog_tag << "Failed to create the SOCom server connector for"
                                   << config.instance_specifier << ":" << connector.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_service_setup_failed);
    }

    m_disabled_connector = std::move(connector).value();
    return {};
}

Result<void> Consumer_service_binding::start() noexcept {
    {
        std::lock_guard const lock{m_mutex};
        if (m_started) {
            return MakeUnexpected(Mw_com_binding_error::logic_error_already_started);
        }
        m_started = true;
    }

    // The handler can already be invoked from within StartFindService, on this very thread. Every
    // member it touches is initialized at this point, and no binding mutex is held across the
    // call so discovery lock order is preserved.
    auto find_handle = score::mw::com::GenericProxy::StartFindService(
        [this](auto handles, auto) noexcept { on_find_service(std::move(handles)); }, m_specifier);
    if (!find_handle.has_value()) {
        {
            std::lock_guard const lock{m_mutex};
            m_started = false;
        }
        score::mw::log::LogError() << kLog_tag << "Failed to start service discovery for"
                                   << m_instance_specifier << ":" << find_handle.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_service_find_failed);
    }

    std::lock_guard const lock{m_mutex};
    m_find_handle = std::move(find_handle).value();
    return {};
}

void Consumer_service_binding::on_find_service(
    score::mw::com::ServiceHandleContainer<score::mw::com::HandleType> handles) noexcept {
    if (handles.empty()) {
        score::mw::log::LogInfo() << kLog_tag << "Bridged service" << m_instance_specifier
                                  << "is gone";
        detach_from_peer();
        return;
    }

    // maxSubscribers is 1 per instance, so there is at most one active peer daemon; use
    // handles.front() consistently across duplicate-handle and replacement checks.
    auto const target_handle = handles.front();
    std::optional<score::mw::com::GenericProxy> outdated_proxy{};
    std::uint64_t expected_epoch{0U};
    {
        std::lock_guard const lifecycle_lock{m_lifecycle_mutex};
        bool need_detach{false};
        {
            std::lock_guard const lock{m_mutex};
            if (m_stopping) {
                return;
            }
            if (m_active_handle.has_value() && (*m_active_handle == target_handle)) {
                return;
            }
            need_detach = m_proxy.has_value() || (m_enabled_connector != nullptr);
            expected_epoch = ++m_discovery_epoch;
        }
        if (need_detach) {
            outdated_proxy = detach_from_peer_locked();
        }
    }
    // Destroy any replaced proxy outside all binding locks.
    outdated_proxy.reset();

    attach_to_peer(target_handle, expected_epoch);
}

void Consumer_service_binding::attach_to_peer(score::mw::com::HandleType handle,
                                              std::uint64_t const expected_epoch) noexcept {
    auto const peer_handle = handle;
    // Outside the locks on purpose: Create() starts the proxy's auto-reconnect discovery and
    // therefore takes the mw::com discovery lock, see the lock order note in the header.
    auto created = score::mw::com::GenericProxy::Create(std::move(handle));
    if (!created.has_value()) {
        score::mw::log::LogError() << kLog_tag << "Failed to create the GenericProxy for"
                                   << m_instance_specifier << ":" << created.error();
        return;
    }

    // Validate and resolve event pointers before taking any binding lock; GenericProxy owns its
    // event map on the heap so event addresses remain stable when created.value() moves into
    // m_proxy.
    auto const resolved_events = resolve_proxy_events(created.value());
    if (!resolved_events.has_value()) {
        return;
    }

    // A proxy superseded while locks were released is carried out of the locked region here
    // because destroying it takes the mw::com discovery lock as well.
    std::optional<score::mw::com::GenericProxy> rejected{};
    bool enabled{false};
    {
        std::lock_guard const lifecycle_lock{m_lifecycle_mutex};

        score::socom::Disabled_server_connector::Uptr disabled{};
        {
            std::lock_guard const lock{m_mutex};

            if (m_stopping || (m_discovery_epoch != expected_epoch) || m_proxy.has_value() ||
                (m_disabled_connector == nullptr)) {
                rejected.emplace(std::move(created).value());
            } else {
                m_proxy.emplace(std::move(created).value());
                for (std::size_t index = 0U; index < m_events.size(); ++index) {
                    m_events[index].proxy_event = (*resolved_events)[index].first;
                    m_events[index].max_payload_size = (*resolved_events)[index].second;
                }
                m_active_handle.emplace(peer_handle);
                disabled = std::move(m_disabled_connector);
            }
        }

        if (disabled != nullptr) {
            // Only under m_lifecycle_mutex: enable() makes local applications connect and
            // subscribe, so it takes SOCom locks that SOCom also holds while invoking
            // on_event_subscription_change(). Callbacks firing inside enable() only record
            // wanted_locally under m_mutex because m_enabled_connector is still null, and the
            // reconcile loop below acts on that state once m_enabled_connector is stored.
            auto enabled_connector =
                score::socom::Disabled_server_connector::enable(std::move(disabled));

            {
                std::lock_guard const lock{m_mutex};
                m_enabled_connector = std::move(enabled_connector);
            }
            for (std::size_t index = 0U; index < m_events.size(); ++index) {
                reconcile_subscription(static_cast<score::socom::Event_id>(index));
            }
            enabled = true;
        }
    }

    // Not under any binding lock, see detach_from_peer().
    rejected.reset();

    if (enabled) {
        score::mw::log::LogInfo() << kLog_tag << "Bridged service" << m_instance_specifier
                                  << "is available";
    }
}

std::optional<std::vector<std::pair<score::mw::com::GenericProxyEvent*, std::size_t>>>
Consumer_service_binding::resolve_proxy_events(
    score::mw::com::GenericProxy const& proxy) const noexcept {
    // Not const: the const find() overload would only hand out a const event.
    auto proxy_events = proxy.GetEvents();
    std::vector<std::pair<score::mw::com::GenericProxyEvent*, std::size_t>> resolved{};
    resolved.reserve(m_events.size());
    for (auto const& event : m_events) {
        auto const entry = proxy_events.find(event.name);
        if (entry == proxy_events.cend()) {
            score::mw::log::LogError()
                << kLog_tag << "Event" << event.name << "of" << m_instance_specifier
                << "is missing from the GenericProxy, the service stays unavailable";
            return std::nullopt;
        }

        // Check that the provider's sample size covers our configured minimum (0 in someipd)
        // without unsigned addition overflow, then adopt the provider's actual max payload size.
        auto const peer_sample_size = entry->second.GetDataTypeSizeInfo().Size();
        if ((peer_sample_size < kSample_prefix_size) ||
            ((peer_sample_size - kSample_prefix_size) < event.configured_max_payload_size)) {
            auto const expected = addressed_sample_size(
                Event_config{event.name, Mw_com_event_id{0U}, event.configured_max_payload_size});
            score::mw::log::LogError()
                << kLog_tag << "Event" << event.name << "of" << m_instance_specifier
                << "has peer sample size" << peer_sample_size << "but at least" << expected
                << "was configured, the service stays unavailable";
            return std::nullopt;
        }

        resolved.emplace_back(&entry->second, peer_sample_size - kSample_prefix_size);
    }
    return resolved;
}

bool Consumer_service_binding::is_subscription_desired(Event const& event) const noexcept {
    return event.wanted_locally && (event.proxy_event != nullptr) &&
           (m_enabled_connector != nullptr);
}

bool Consumer_service_binding::can_receive_samples(Event const& event) const noexcept {
    return event.subscribed && is_subscription_desired(event);
}

void Consumer_service_binding::detach_from_peer() noexcept {
    std::optional<score::mw::com::GenericProxy> proxy{};
    {
        std::lock_guard const lifecycle_lock{m_lifecycle_mutex};
        {
            std::lock_guard const lock{m_mutex};
            ++m_discovery_epoch;
        }
        proxy = detach_from_peer_locked();
    }

    // Not under any binding lock, see the lock order note in the header. Nothing else can reach
    // this proxy any more: it was swapped out and every event pointer into it was cleared under
    // m_mutex, so dropping it late is safe.
    proxy.reset();
}

std::optional<score::mw::com::GenericProxy>
Consumer_service_binding::detach_from_peer_locked() noexcept {
    std::optional<score::mw::com::GenericProxy> proxy{};

    std::vector<score::mw::com::GenericProxyEvent*> subscribed{};
    subscribed.reserve(m_events.size());
    {
        std::unique_lock<std::mutex> lock{m_mutex};
        m_detaching = true;
        m_reconcile_cv.wait(lock, [this]() noexcept {
            return std::none_of(m_events.cbegin(), m_events.cend(),
                                [](Event const& event) noexcept { return event.reconciling; });
        });
        for (auto& event : m_events) {
            if (event.subscribed && (event.proxy_event != nullptr)) {
                subscribed.push_back(event.proxy_event);
            }
            event.subscribed = false;
        }
    }

    // Stop receive handlers before disabling the connector: UnsetReceiveHandler() blocks until
    // all running on_samples_available() calls (including update_event()) have returned, while
    // m_enabled_connector is still alive and no SOCom lock is held yet.
    for (auto* const proxy_event : subscribed) {
        (void)proxy_event->UnsetReceiveHandler();
    }

    score::socom::Enabled_server_connector::Uptr enabled{};
    {
        std::lock_guard const lock{m_mutex};
        enabled = std::move(m_enabled_connector);
    }
    if (enabled != nullptr) {
        auto disabled = score::socom::Enabled_server_connector::disable(std::move(enabled));
        std::lock_guard const lock{m_mutex};
        m_disabled_connector = std::move(disabled);
    }

    // Unsubscribe only after disable() has disconnected local SOCom clients and released any
    // SamplePtr-backed payloads; LoLa aborts if a subscription outlives its event or is dropped
    // while SamplePtrs are still held.
    for (auto* const proxy_event : subscribed) {
        proxy_event->Unsubscribe();
    }

    {
        std::lock_guard const lock{m_mutex};
        std::swap(proxy, m_proxy);
        m_active_handle.reset();
        for (auto& event : m_events) {
            event.proxy_event = nullptr;
            event.wanted_locally = false;
        }
        m_detaching = false;
    }

    return proxy;
}

void Consumer_service_binding::on_event_subscription_change(score::socom::Event_id const event_id,
                                                            bool const subscribed) noexcept {
    {
        std::lock_guard const lock{m_mutex};
        if (m_stopping || (event_id >= m_events.size())) {
            return;
        }
        auto& event = m_events[event_id];
        event.wanted_locally = subscribed;
        if (is_subscription_desired(event) == event.subscribed) {
            return;
        }
    }

    // LoLa holds its per-event handler lock and thread-local handler context across the entire
    // receive callback, so any subscription change triggered synchronously inside update_event()
    // must reconcile on a separate worker thread.
    if (g_receive_handler_depth > 0U) {
        m_reconcile_worker.Post([this, event_id](score::cpp::stop_token const&) noexcept {
            reconcile_subscription(event_id);
        });
        return;
    }

    reconcile_subscription(event_id);
}

void Consumer_service_binding::reconcile_subscription(
    score::socom::Event_id const event_id) noexcept {
    std::unique_lock<std::mutex> lock{m_mutex};
    auto& event = m_events[event_id];
    if (m_stopping || m_detaching || event.reconciling ||
        (is_subscription_desired(event) == event.subscribed)) {
        return;
    }

    event.reconciling = true;
    while (!m_stopping && !m_detaching) {
        auto const desired = is_subscription_desired(event);
        if (desired == event.subscribed) {
            break;
        }

        auto* const proxy_event = event.proxy_event;
        auto const max_sample_count = event.max_sample_count;
        if (desired) {
            lock.unlock();
            auto const result = proxy_event->Subscribe(max_sample_count);
            if (!result.has_value()) {
                score::mw::log::LogError()
                    << kLog_tag << "Failed to subscribe to event" << event.name << "of"
                    << m_instance_specifier << ":" << result.error();
                lock.lock();
                break;
            }
            lock.lock();
            if (m_stopping || m_detaching || !is_subscription_desired(event)) {
                lock.unlock();
                proxy_event->Unsubscribe();
                lock.lock();
                continue;
            }
            event.subscribed = true;
            lock.unlock();

            auto const handler_set = proxy_event->SetReceiveHandler(
                [this, event_id]() noexcept { on_samples_available(event_id); });
            if (!handler_set.has_value()) {
                score::mw::log::LogError()
                    << kLog_tag << "Failed to set the receive handler for event" << event.name
                    << "of" << m_instance_specifier << ":" << handler_set.error();
                lock.lock();
                event.subscribed = false;
                lock.unlock();
                proxy_event->Unsubscribe();
                lock.lock();
                break;
            }
            lock.lock();
        } else {
            event.subscribed = false;
            lock.unlock();
            (void)proxy_event->UnsetReceiveHandler();
            proxy_event->Unsubscribe();
            lock.lock();
        }
    }
    event.reconciling = false;
    m_reconcile_cv.notify_all();
}

void Consumer_service_binding::on_samples_available(
    score::socom::Event_id const event_id) noexcept {
    Event* event_ptr{nullptr};
    {
        std::lock_guard const lock{m_mutex};
        if (event_id >= m_events.size()) {
            return;
        }
        if (!can_receive_samples(m_events[event_id])) {
            return;
        }
        event_ptr = &m_events[event_id];
    }

    auto& event = *event_ptr;
    std::lock_guard<std::mutex> const rx_lock{event.rx_mutex};

    score::mw::com::GenericProxyEvent* proxy_event{nullptr};
    std::size_t max_sample_count{0U};
    {
        std::lock_guard const lock{m_mutex};
        if (!can_receive_samples(event)) {
            return;
        }
        proxy_event = event.proxy_event;
        max_sample_count = event.max_sample_count;
    }

    ++g_receive_handler_depth;

    // Step 1: drain all available samples from LoLa into the pre-reserved batch so
    // GetNewSamples() returns and ~TrackerGuardFactory() releases unused slot guards before any
    // SOCom callback can fire.
    event.sample_batch.clear();
    auto const received = proxy_event->GetNewSamples(
        [&batch = event.sample_batch](score::mw::com::SamplePtr<void> sample) noexcept {
            if (sample.get() != nullptr) {
                batch.emplace_back(std::move(sample));
            }
        },
        max_sample_count);
    if (!received.has_value()) {
        score::mw::log::LogWarn() << kLog_tag << "Failed to get new samples of event" << event.name
                                  << "of" << m_instance_specifier << ":" << received.error();
    }

    // Step 2: forward drained samples via update_event() with m_mutex unlocked.
    for (auto& slot : event.sample_batch) {
        auto sample = std::move(slot);
        auto const* const sample_base = static_cast<std::byte const*>(sample.get());
        auto const length = read_payload_length(sample_base);

        score::socom::Enabled_server_connector* target_connector{nullptr};
        std::size_t max_payload_size{0U};
        {
            std::lock_guard const lock{m_mutex};
            if (!can_receive_samples(event)) {
                break;
            }
            max_payload_size = event.max_payload_size;
            target_connector = m_enabled_connector.get();
        }

        if (length > max_payload_size) {
            score::mw::log::LogError()
                << kLog_tag << "Dropping a sample of event" << event.name << "of"
                << m_instance_specifier << ": payload length" << length
                << "exceeds the peer's maximum of" << max_payload_size;
            continue;
        }

        score::socom::Payload payload{
            score::socom::Payload::Span{sample_base, kSample_prefix_size + length}.subspan(
                kSample_prefix_size),
            std::move(sample)};

        auto const updated = target_connector->update_event(event_id, std::move(payload));
        if (!updated.has_value()) {
            score::mw::log::LogWarn() << kLog_tag << "Failed to forward event" << event.name << "of"
                                      << m_instance_specifier << ":" << updated.error();
        }
    }
    event.sample_batch.clear();
    --g_receive_handler_depth;
}

}  // namespace score::gateway_ipc_binding::mw_com
