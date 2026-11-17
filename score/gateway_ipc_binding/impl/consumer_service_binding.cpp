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

/// \brief Decrements the in-flight `update_event()` counter under `m_mutex` on scope exit.
class In_flight_update_guard final {
   public:
    explicit In_flight_update_guard(std::recursive_mutex& mutex, std::size_t& in_flight_updates,
                                    std::condition_variable_any& updates_drained_cv) noexcept
        : m_mutex{mutex},
          m_in_flight_updates{in_flight_updates},
          m_updates_drained_cv{updates_drained_cv} {}

    ~In_flight_update_guard() noexcept {
        std::lock_guard const lock{m_mutex};
        --m_in_flight_updates;
        if (m_in_flight_updates == 0U) {
            m_updates_drained_cv.notify_all();
        }
    }

    In_flight_update_guard(In_flight_update_guard const&) = delete;
    In_flight_update_guard& operator=(In_flight_update_guard const&) = delete;
    In_flight_update_guard(In_flight_update_guard&&) = delete;
    In_flight_update_guard& operator=(In_flight_update_guard&&) = delete;

   private:
    std::recursive_mutex& m_mutex;
    std::size_t& m_in_flight_updates;
    std::condition_variable_any& m_updates_drained_cv;
};

}  // namespace

Consumer_service_binding::Consumer_service_binding(
    std::vector<Event> events, std::string instance_specifier,
    score::mw::com::InstanceSpecifier specifier) noexcept
    : m_events{std::move(events)},
      m_instance_specifier{std::move(instance_specifier)},
      m_specifier{std::move(specifier)} {}

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

    std::vector<Event> events{};
    events.reserve(config.events.size());
    for (auto const& event : config.events) {
        events.push_back(Event{event.name, event.max_payload_size, config.max_sample_count});
    }

    // Not make_unique: the constructor is private.
    std::unique_ptr<Consumer_service_binding> binding{new Consumer_service_binding{
        std::move(events), config.instance_specifier, std::move(specifier).value()}};

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
            // representable. Known gap, see docs/mw_com_binding.rst.
            return nullptr;
        },
        [this](score::socom::Enabled_server_connector&, score::socom::Event_id const event_id,
               score::socom::Event_state const state) {
            on_event_subscription_change(event_id, state == score::socom::Event_state::subscribed);
        },
        [](score::socom::Enabled_server_connector&, score::socom::Event_id const event_id) {
            // LoLa has no pull API on the proxy side. Known gap, see docs/mw_com_binding.rst.
            score::mw::log::LogWarn()
                << kLog_tag << "Ignoring the update request for event" << event_id
                << ": mw::com cannot serve requested event updates";
        },
        [](score::socom::Enabled_server_connector&,
           score::socom::Method_id) -> Result<score::socom::Writable_payload> {
            return score::socom::Writable_payload{score::socom::Writable_payload::Writable_span{},
                                                  score::socom::kNoSlotHandle, []() {}};
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

    // A proxy that turns out to be unusable or superseded is carried out of the locked region here
    // because destroying it takes the mw::com discovery lock as well.
    std::optional<score::mw::com::GenericProxy> rejected{};
    bool enabled{false};
    {
        std::lock_guard const lifecycle_lock{m_lifecycle_mutex};

        score::socom::Disabled_server_connector::Uptr disabled{};
        {
            std::lock_guard const subscription_lock{m_subscription_mutex};
            std::lock_guard const lock{m_mutex};

            if (m_stopping || (m_discovery_epoch != expected_epoch) || m_proxy.has_value() ||
                (m_disabled_connector == nullptr)) {
                rejected.emplace(std::move(created).value());
            } else {
                m_proxy.emplace(std::move(created).value());

                if (bind_proxy_events()) {
                    m_active_handle.emplace(peer_handle);
                    disabled = std::move(m_disabled_connector);
                } else {
                    std::swap(rejected, m_proxy);
                }
            }
        }

        if (disabled != nullptr) {
            // Only under m_lifecycle_mutex: enable() makes local applications connect and
            // subscribe, so it takes SOCom locks that SOCom also holds while it calls
            // on_event_subscription_change() from other threads. Holding a mutex that callback
            // needs across enable() would invert the lock order.
            //
            // Those callbacks therefore run to completion here. They only record the local
            // demand, because m_enabled_connector is not in place yet, and the reconcile loop
            // below acts on it.
            auto enabled_connector =
                score::socom::Disabled_server_connector::enable(std::move(disabled));

            std::lock_guard const subscription_lock{m_subscription_mutex};
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

bool Consumer_service_binding::bind_proxy_events() noexcept {
    // Not const: the const find() overload would only hand out a const event.
    auto proxy_events = m_proxy->GetEvents();
    for (auto& event : m_events) {
        auto const entry = proxy_events.find(event.name);
        if (entry == proxy_events.cend()) {
            score::mw::log::LogError()
                << kLog_tag << "Event" << event.name << "of" << m_instance_specifier
                << "is missing from the GenericProxy, the service stays unavailable";
            clear_proxy_events();
            return false;
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
            clear_proxy_events();
            return false;
        }

        event.max_payload_size = peer_sample_size - kSample_prefix_size;
        event.proxy_event = &entry->second;
    }
    return true;
}

void Consumer_service_binding::clear_proxy_events() noexcept {
    for (auto& event : m_events) {
        event.proxy_event = nullptr;
    }
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

    // Move m_enabled_connector out under m_mutex so no new update_event() can start, then wait
    // with m_mutex released until every in-flight update_event() has finished before calling
    // disable(). Disabling blocks until running SOCom callbacks finish, and those callbacks take
    // m_subscription_mutex and m_mutex, so disable() runs only under m_lifecycle_mutex.
    score::socom::Enabled_server_connector::Uptr enabled{};
    {
        std::unique_lock<std::recursive_mutex> lock{m_mutex};
        enabled = std::move(m_enabled_connector);
        m_updates_drained_cv.wait(lock, [this]() noexcept { return m_in_flight_updates == 0U; });
    }
    if (enabled != nullptr) {
        auto disabled = score::socom::Enabled_server_connector::disable(std::move(enabled));
        std::lock_guard const lock{m_mutex};
        m_disabled_connector = std::move(disabled);
    }

    std::lock_guard const subscription_lock{m_subscription_mutex};

    std::vector<score::mw::com::GenericProxyEvent*> subscribed{};
    {
        std::lock_guard const lock{m_mutex};
        std::swap(proxy, m_proxy);
        m_active_handle.reset();
        for (auto& event : m_events) {
            if (event.subscribed && (event.proxy_event != nullptr)) {
                subscribed.push_back(event.proxy_event);
            }
            event.proxy_event = nullptr;
            event.wanted_locally = false;
            event.subscribed = false;
        }
    }

    // Not under m_mutex: both block until a running receive handler has finished, and that
    // handler needs m_mutex. Unlike a generated proxy, a GenericProxy does not unsubscribe
    // its events when it is destroyed, and LoLa aborts if a subscription outlives its event.
    for (auto* const proxy_event : subscribed) {
        (void)proxy_event->UnsetReceiveHandler();
        proxy_event->Unsubscribe();
    }

    return proxy;
}

void Consumer_service_binding::on_event_subscription_change(score::socom::Event_id const event_id,
                                                            bool const subscribed) noexcept {
    std::lock_guard const subscription_lock{m_subscription_mutex};
    {
        std::lock_guard const lock{m_mutex};
        if (event_id >= m_events.size()) {
            return;
        }
        m_events[event_id].wanted_locally = subscribed;
    }
    reconcile_subscription(event_id);
}

void Consumer_service_binding::reconcile_subscription(
    score::socom::Event_id const event_id) noexcept {
    score::mw::com::GenericProxyEvent* proxy_event{nullptr};
    std::size_t max_sample_count{0U};
    bool desired{false};
    {
        std::lock_guard const lock{m_mutex};
        if ((m_enabled_connector == nullptr) && (m_in_flight_updates > 0U)) {
            // Detach is draining in-flight updates before disable() and will tear down all event
            // subscriptions once disable() returns.
            return;
        }
        auto const& event = m_events[event_id];
        // While the connector is not enabled yet, attach_to_peer() has not finished and will
        // reconcile again once it has.
        desired = event.wanted_locally && (event.proxy_event != nullptr) &&
                  (m_enabled_connector != nullptr);
        if (desired == event.subscribed) {
            return;
        }
        proxy_event = event.proxy_event;
        max_sample_count = event.max_sample_count;
    }

    if (proxy_event == nullptr) {
        return;
    }

    if (desired) {
        auto const result = proxy_event->Subscribe(max_sample_count);
        if (!result.has_value()) {
            score::mw::log::LogError()
                << kLog_tag << "Failed to subscribe to event" << m_events[event_id].name << "of"
                << m_instance_specifier << ":" << result.error();
            return;
        }
        {
            std::lock_guard const lock{m_mutex};
            m_events[event_id].subscribed = true;
        }
        auto const handler_set = proxy_event->SetReceiveHandler(
            [this, event_id]() noexcept { on_samples_available(event_id); });
        if (!handler_set.has_value()) {
            score::mw::log::LogError() << kLog_tag << "Failed to set the receive handler for event"
                                       << m_events[event_id].name << "of" << m_instance_specifier
                                       << ":" << handler_set.error();
            {
                std::lock_guard const lock{m_mutex};
                m_events[event_id].subscribed = false;
            }
            proxy_event->Unsubscribe();
            return;
        }
    } else {
        {
            std::lock_guard const lock{m_mutex};
            m_events[event_id].subscribed = false;
        }
        (void)proxy_event->UnsetReceiveHandler();
        proxy_event->Unsubscribe();
    }
}

void Consumer_service_binding::on_samples_available(
    score::socom::Event_id const event_id) noexcept {
    score::mw::com::GenericProxyEvent* proxy_event{nullptr};
    std::size_t max_sample_count{0U};
    {
        std::lock_guard const lock{m_mutex};
        if (event_id >= m_events.size()) {
            return;
        }
        auto const& event = m_events[event_id];
        if (!event.subscribed || (event.proxy_event == nullptr) ||
            (m_enabled_connector == nullptr)) {
            // Torn down or unsubscribed while this handler was waiting for the lock.
            return;
        }
        proxy_event = event.proxy_event;
        max_sample_count = event.max_sample_count;
    }

    // Release m_mutex before GetNewSamples() and update_event(): SOCom callbacks triggered inside
    // update_event() acquire m_subscription_mutex before m_mutex, so holding m_mutex here would
    // invert the lock hierarchy across threads. UnsetReceiveHandler() blocks detach_from_peer()
    // before m_proxy is destroyed, keeping proxy_event valid across GetNewSamples(). Each
    // update_event() call increments m_in_flight_updates under m_mutex so detach_from_peer_locked()
    // waits on m_updates_drained_cv before calling Enabled_server_connector::disable(), keeping
    // target_connector alive and enabled until update_event() returns.
    auto const received = proxy_event->GetNewSamples(
        [this, event_id](score::mw::com::SamplePtr<void> sample) noexcept {
            auto const* const sample_base = static_cast<std::byte const*>(sample.get());
            if (sample_base == nullptr) {
                return;
            }

            auto const length = read_payload_length(sample_base);

            score::socom::Enabled_server_connector* target_connector{nullptr};
            std::size_t max_payload_size{0U};
            bool length_exceeded{false};
            {
                std::lock_guard const lock{m_mutex};
                auto const& event = m_events[event_id];
                if (!event.subscribed || (event.proxy_event == nullptr) ||
                    (m_enabled_connector == nullptr)) {
                    return;
                }
                max_payload_size = event.max_payload_size;
                if (length > max_payload_size) {
                    length_exceeded = true;
                } else {
                    target_connector = m_enabled_connector.get();
                    ++m_in_flight_updates;
                }
            }

            if (length_exceeded) {
                score::mw::log::LogError()
                    << kLog_tag << "Dropping a sample of event" << m_events[event_id].name << "of"
                    << m_instance_specifier << ": payload length" << length
                    << "exceeds the peer's maximum of" << max_payload_size;
                return;
            }

            In_flight_update_guard const update_guard{m_mutex, m_in_flight_updates,
                                                      m_updates_drained_cv};

            score::socom::Payload payload{
                score::socom::Payload::Span{sample_base, kSample_prefix_size + length}.subspan(
                    kSample_prefix_size),
                score::socom::kNoSlotHandle,
                // Owning the SamplePtr here is what returns the slot to LoLa once the receiving
                // application is done with the payload. It is boxed because a SamplePtr does not
                // fit into the inline storage of socom::Payload::Payload_destroyed.
                [sample = std::make_unique<score::mw::com::SamplePtr<void>>(
                     std::move(sample))]() noexcept {}};

            auto const updated = target_connector->update_event(event_id, std::move(payload));
            if (!updated.has_value()) {
                score::mw::log::LogWarn()
                    << kLog_tag << "Failed to forward event" << m_events[event_id].name << "of"
                    << m_instance_specifier << ":" << updated.error();
            }
        },
        max_sample_count);

    if (!received.has_value()) {
        score::mw::log::LogWarn() << kLog_tag << "Failed to get new samples of event"
                                  << m_events[event_id].name << "of" << m_instance_specifier << ":"
                                  << received.error();
    }
}

}  // namespace score::gateway_ipc_binding::mw_com
