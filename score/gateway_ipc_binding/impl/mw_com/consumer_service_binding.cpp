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

#include <memory>
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

}  // namespace

Consumer_service_binding::Consumer_service_binding(std::vector<Event> events,
                                                   std::string instance_specifier) noexcept
    : m_events{std::move(events)}, m_instance_specifier{std::move(instance_specifier)} {}

Consumer_service_binding::~Consumer_service_binding() noexcept {
    // See docs/mw_com_binding.rst, section "Teardown". Stop discovery first, so that no handler
    // can race with the teardown below.
    std::optional<score::mw::com::FindServiceHandle> find_handle{};
    {
        std::lock_guard const lock{m_mutex};
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
        events.push_back(
            Event{event.name, event.header_size, event.max_payload_size, config.max_sample_count});
    }

    // Not make_unique: the constructor is private.
    std::unique_ptr<Consumer_service_binding> binding{
        new Consumer_service_binding{std::move(events), config.instance_specifier}};

    auto created = binding->create_server_connector(runtime, config);
    if (!created.has_value()) {
        return MakeUnexpected<std::unique_ptr<Consumer_service_binding>>(
            std::move(created).error());
    }

    auto started = binding->start_find_service(std::move(specifier).value());
    if (!started.has_value()) {
        return MakeUnexpected<std::unique_ptr<Consumer_service_binding>>(
            std::move(started).error());
    }

    return binding;
}

Result<void> Consumer_service_binding::create_server_connector(
    score::socom::Runtime& runtime, Service_config const& config) noexcept {
    score::socom::Disabled_server_connector::Callbacks callbacks{
        [](score::socom::Enabled_server_connector&, score::socom::Method_id, score::socom::Payload,
           score::socom::Method_call_reply_data_opt,
           score::socom::Posix_credentials const&) -> score::socom::Method_invocation::Uptr {
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

Result<void> Consumer_service_binding::start_find_service(
    score::mw::com::InstanceSpecifier specifier) noexcept {
    // The handler can already be invoked from within StartFindService, on this very thread. Every
    // member it touches is initialized at this point, so that is safe.
    auto find_handle = score::mw::com::GenericProxy::StartFindService(
        [this](auto handles, auto) noexcept { on_find_service(std::move(handles)); },
        std::move(specifier));
    if (!find_handle.has_value()) {
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
    detach_from_peer();

    if (handles.empty()) {
        score::mw::log::LogInfo() << kLog_tag << "Bridged service" << m_instance_specifier
                                  << "is gone";
        return;
    }

    // maxSubscribers is 1 per instance, so there is exactly one peer daemon. Should the
    // deployment ever offer more, the first handle is as good as any.
    attach_to_peer(handles.front());
}

void Consumer_service_binding::attach_to_peer(score::mw::com::HandleType handle) noexcept {
    // Outside the locks on purpose: Create() starts the proxy's auto-reconnect discovery and
    // therefore takes the mw::com discovery lock, see the lock order note in the header.
    auto created = score::mw::com::GenericProxy::Create(std::move(handle));
    if (!created.has_value()) {
        score::mw::log::LogError() << kLog_tag << "Failed to create the GenericProxy for"
                                   << m_instance_specifier << ":" << created.error();
        return;
    }

    // A proxy that turns out to be unusable is carried out of the locked region in here, because
    // destroying it takes the mw::com discovery lock as well.
    std::optional<score::mw::com::GenericProxy> rejected{};
    bool enabled{false};
    {
        std::lock_guard const lifecycle_lock{m_lifecycle_mutex};

        score::socom::Disabled_server_connector::Uptr disabled{};
        {
            std::lock_guard const subscription_lock{m_subscription_mutex};
            std::lock_guard const lock{m_mutex};

            m_proxy.emplace(std::move(created).value());

            if (bind_proxy_events()) {
                disabled = std::move(m_disabled_connector);
            } else {
                std::swap(rejected, m_proxy);
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

        // The peer laid the samples out for its own configuration. If the two deployments
        // disagree, the payload would be read at the wrong offset.
        auto const peer_sample_size = entry->second.GetSampleSize();
        auto const expected = kSample_prefix_size + event.header_size + event.max_payload_size;
        if (peer_sample_size < expected) {
            score::mw::log::LogError()
                << kLog_tag << "Event" << event.name << "of" << m_instance_specifier
                << "has peer sample size" << peer_sample_size << "but at least" << expected
                << "was configured, the service stays unavailable";
            clear_proxy_events();
            return false;
        }

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

        // Disabling blocks until running SOCom callbacks have finished, and those callbacks take
        // m_subscription_mutex and m_mutex. It must therefore run before this thread acquires
        // either of them, and only under m_lifecycle_mutex, which no callback takes.
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

        std::lock_guard const subscription_lock{m_subscription_mutex};

        std::vector<score::mw::com::GenericProxyEvent*> subscribed{};
        {
            std::lock_guard const lock{m_mutex};
            std::swap(proxy, m_proxy);
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
    }

    // Not under any binding lock, see the lock order note in the header. Nothing else can reach
    // this proxy any more: it was swapped out and every event pointer into it was cleared under
    // m_mutex, so dropping it late is safe.
    proxy.reset();
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
        auto const handler_set = proxy_event->SetReceiveHandler(
            [this, event_id]() noexcept { on_samples_available(event_id); });
        if (!handler_set.has_value()) {
            score::mw::log::LogError() << kLog_tag << "Failed to set the receive handler for event"
                                       << m_events[event_id].name << "of" << m_instance_specifier
                                       << ":" << handler_set.error();
            proxy_event->Unsubscribe();
            return;
        }
    } else {
        (void)proxy_event->UnsetReceiveHandler();
        proxy_event->Unsubscribe();
    }

    std::lock_guard const lock{m_mutex};
    m_events[event_id].subscribed = desired;
}

void Consumer_service_binding::on_samples_available(
    score::socom::Event_id const event_id) noexcept {
    std::lock_guard const lock{m_mutex};

    if (event_id >= m_events.size()) {
        return;
    }
    auto& event = m_events[event_id];
    if ((event.proxy_event == nullptr) || (m_enabled_connector == nullptr)) {
        // Torn down while this handler was waiting for the lock.
        return;
    }

    auto const received = event.proxy_event->GetNewSamples(
        [this, &event, event_id](score::mw::com::SamplePtr<void> sample) noexcept {
            auto const* const sample_base = static_cast<std::byte const*>(sample.get());
            if (sample_base == nullptr) {
                return;
            }

            auto const length = read_payload_length(sample_base);
            if (length > event.max_payload_size) {
                score::mw::log::LogError()
                    << kLog_tag << "Dropping a sample of event" << event.name << "of"
                    << m_instance_specifier << ": payload length" << length
                    << "exceeds the configured maximum of" << event.max_payload_size;
                return;
            }

            // Turning a read-only sample into a writable socom::Payload needs a const_cast, the
            // same wart the shared-memory read path of the message_passing implementation has.
            // Neither this binding nor the receiving application writes to it.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): see above
            auto* const writable_base = const_cast<std::byte*>(sample_base);
            score::socom::Payload::Writable_span const span{
                writable_base, kSample_prefix_size + event.header_size + length};

            score::socom::Payload payload{
                span, score::socom::kNoSlotHandle,
                // Owning the SamplePtr here is what returns the slot to LoLa once the receiving
                // application is done with the payload. It is boxed because a SamplePtr does not
                // fit into the inline storage of socom::Payload::Payload_destroyed.
                [sample = std::make_unique<score::mw::com::SamplePtr<void>>(
                     std::move(sample))]() noexcept {},
                event.header_size, kSample_prefix_size};

            auto const updated = m_enabled_connector->update_event(event_id, std::move(payload));
            if (!updated.has_value()) {
                score::mw::log::LogWarn() << kLog_tag << "Failed to forward event" << event.name
                                          << "of" << m_instance_specifier << ":" << updated.error();
            }
        },
        event.max_sample_count);

    if (!received.has_value()) {
        score::mw::log::LogWarn() << kLog_tag << "Failed to get new samples of event" << event.name
                                  << "of" << m_instance_specifier << ":" << received.error();
    }
}

}  // namespace score::gateway_ipc_binding::mw_com
