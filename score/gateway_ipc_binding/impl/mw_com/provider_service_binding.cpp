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

#include "provider_service_binding.hpp"

#include <string_view>
#include <utility>

#include "sample_layout.hpp"
#include "score/gateway_ipc_binding/error.hpp"
#include "score/mw/log/logging.h"

namespace score::gateway_ipc_binding::mw_com {

namespace {

/// \brief Log prefix shared by the whole binding
constexpr std::string_view kLog_tag{"[gateway_ipc_binding]"};

}  // namespace

std::size_t Provider_service_binding::Allocations::insert(
    score::mw::com::SampleAllocateePtr<void> sample) noexcept {
    std::lock_guard const lock{m_mutex};
    auto const slot_handle = m_next_slot_handle;
    ++m_next_slot_handle;
    m_in_flight.emplace(slot_handle, std::move(sample));
    return slot_handle;
}

std::optional<score::mw::com::SampleAllocateePtr<void>> Provider_service_binding::Allocations::take(
    std::size_t const slot_handle) noexcept {
    std::lock_guard const lock{m_mutex};
    auto const entry = m_in_flight.find(slot_handle);
    if (entry == m_in_flight.end()) {
        return std::nullopt;
    }
    auto sample = std::move(entry->second);
    m_in_flight.erase(entry);
    return sample;
}

void Provider_service_binding::Allocations::clear() noexcept {
    std::lock_guard const lock{m_mutex};
    m_in_flight.clear();
}

Provider_service_binding::Provider_service_binding(
    score::mw::com::GenericSkeleton skeleton) noexcept
    : m_skeleton{std::move(skeleton)} {}

Provider_service_binding::~Provider_service_binding() noexcept {
    // Teardown order matters, LoLa terminates the process if a SampleAllocateePtr outlives its
    // event. See docs/mw_com_binding.rst, section "Teardown".

    // 1. Stop the mw::com callbacks. Not under the lock: unsetting synchronizes with a handler
    //    that may currently be waiting for exactly that lock.
    for (auto& event : m_events) {
        auto const unset = event.skeleton_event->UnsetReceiveHandlerRegistrationChangedHandler();
        if (!unset.has_value()) {
            score::mw::log::LogWarn()
                << kLog_tag
                << "Failed to unset the receive handler registration handler:" << unset.error();
        }
    }

    // 2. Stop the SOCom callbacks. The destructor blocks until running callbacks are done, so it
    //    must not run while this object's lock is held.
    score::socom::Client_connector::Uptr connector{};
    {
        std::lock_guard const lock{m_mutex};
        connector = std::move(m_client_connector);
    }
    connector.reset();

    // 3. Nothing calls back anymore. Release the slots, then the skeleton that owns them.
    m_skeleton.StopOfferService();
    m_allocations->clear();
}

Result<std::unique_ptr<Provider_service_binding>> Provider_service_binding::create(
    score::socom::Runtime& runtime, Service_config const& config) noexcept {
    auto specifier = make_instance_specifier(config.instance_specifier);
    if (!specifier.has_value()) {
        return MakeUnexpected<std::unique_ptr<Provider_service_binding>>(
            std::move(specifier).error());
    }

    std::vector<score::mw::com::EventInfo> event_infos{};
    event_infos.reserve(config.events.size());
    for (auto const& event : config.events) {
        event_infos.push_back(score::mw::com::EventInfo{event.name, sample_meta_info(event)});
    }

    score::mw::com::GenericSkeletonServiceElementInfo element_info{};
    element_info.events = event_infos;

    auto skeleton = score::mw::com::GenericSkeleton::Create(specifier.value(), element_info);
    if (!skeleton.has_value()) {
        score::mw::log::LogError() << kLog_tag << "Failed to create the GenericSkeleton for"
                                   << config.instance_specifier << ":" << skeleton.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_service_setup_failed);
    }

    // Not make_unique: the constructor is private.
    std::unique_ptr<Provider_service_binding> binding{
        new Provider_service_binding{std::move(skeleton).value()}};

    auto resolved = binding->resolve_events(config);
    if (!resolved.has_value()) {
        return MakeUnexpected<std::unique_ptr<Provider_service_binding>>(
            std::move(resolved).error());
    }

    auto registered = binding->register_subscription_handlers();
    if (!registered.has_value()) {
        return MakeUnexpected<std::unique_ptr<Provider_service_binding>>(
            std::move(registered).error());
    }

    auto connected = binding->create_client_connector(runtime, config);
    if (!connected.has_value()) {
        return MakeUnexpected<std::unique_ptr<Provider_service_binding>>(
            std::move(connected).error());
    }

    return binding;
}

Result<void> Provider_service_binding::resolve_events(Service_config const& config) noexcept {
    // Not const: the const find() overload would only hand out a const event.
    auto skeleton_events = m_skeleton.GetEvents();
    m_events.reserve(config.events.size());

    for (auto const& event : config.events) {
        auto const entry = skeleton_events.find(event.name);
        if (entry == skeleton_events.cend()) {
            score::mw::log::LogError()
                << kLog_tag << "Event" << event.name << "of" << config.instance_specifier
                << "is missing from the GenericSkeleton";
            return MakeUnexpected(Mw_com_binding_error::runtime_error_service_setup_failed);
        }

        // The size that LoLa actually laid out has to match what the deployment was sized for,
        // otherwise the peer would read the payload at the wrong offset.
        auto const size_info = entry->second.GetSizeInfo();
        if (size_info.size != sample_size(event)) {
            score::mw::log::LogError()
                << kLog_tag << "Event" << event.name << "of" << config.instance_specifier
                << "has sample size" << size_info.size << "but" << sample_size(event)
                << "was configured";
            return MakeUnexpected(Mw_com_binding_error::runtime_error_sample_size_mismatch);
        }

        m_events.push_back(Event{&entry->second, event.header_size, addressed_sample_size(event)});
    }

    return {};
}

Result<void> Provider_service_binding::register_subscription_handlers() noexcept {
    for (std::size_t index = 0U; index < m_events.size(); ++index) {
        auto const event_id = static_cast<score::socom::Event_id>(index);
        auto const registered =
            m_events[index].skeleton_event->SetReceiveHandlerRegistrationChangedHandler(
                [this, event_id](bool const wanted) noexcept {
                    on_peer_interest_change(event_id, wanted);
                });
        if (!registered.has_value()) {
            score::mw::log::LogError()
                << kLog_tag << "Failed to register the receive handler registration handler:"
                << registered.error();
            return MakeUnexpected(Mw_com_binding_error::runtime_error_service_setup_failed);
        }
    }
    return {};
}

Result<void> Provider_service_binding::create_client_connector(
    score::socom::Runtime& runtime, Service_config const& config) noexcept {
    score::socom::Client_connector::Callbacks callbacks{
        [this](score::socom::Client_connector const&, score::socom::Service_state const state,
               score::socom::Server_service_interface_definition const&) {
            on_service_state_change(state);
        },
        [this](score::socom::Client_connector const&, score::socom::Event_id const event_id,
               score::socom::Payload payload) { on_event_update(event_id, std::move(payload)); },
        [](score::socom::Client_connector const&, score::socom::Event_id const event_id,
           score::socom::Payload) {
            // Requested updates have no LoLa counterpart: LoLa offers no pull API on the proxy
            // side, so a field-style initial value cannot be served on demand. Known gap, see
            // docs/mw_com_binding.rst.
            score::mw::log::LogWarn()
                << kLog_tag << "Dropping the requested update of event" << event_id
                << ": mw::com cannot transport requested event updates";
        },
        [this](score::socom::Client_connector const&, score::socom::Event_id const event_id) {
            return on_event_payload_allocate(event_id);
        }};

    auto connector =
        runtime.make_client_connector(score::socom::Service_interface_definition{config.interface},
                                      config.instance, std::move(callbacks));
    if (!connector.has_value()) {
        score::mw::log::LogError() << kLog_tag << "Failed to create the SOCom client connector for"
                                   << config.instance_specifier << ":" << connector.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_service_setup_failed);
    }

    std::lock_guard const lock{m_mutex};
    m_client_connector = std::move(connector).value();
    // make_client_connector() already reports an available service through
    // on_service_state_change(), possibly before it returns. Those callbacks could not subscribe
    // yet because the connector they would subscribe on did not exist, so catch up here.
    for (std::size_t index = 0U; index < m_events.size(); ++index) {
        reconcile_subscription(static_cast<score::socom::Event_id>(index));
    }
    return {};
}

void Provider_service_binding::on_service_state_change(
    score::socom::Service_state const state) noexcept {
    bool const available = state == score::socom::Service_state::available;

    // Offering and withdrawing the offer must not run under m_mutex, see the lock order note in
    // the header. This mutex serializes the two transitions against each other instead.
    std::lock_guard const offer_lock{m_offer_mutex};

    {
        std::lock_guard const lock{m_mutex};
        if (available == m_service_available) {
            return;
        }
        m_service_available = available;

        if (!available) {
            // SOCom drops all subscriptions when the service leaves the available state, so the
            // flags are cleared without calling SOCom back.
            for (auto& event : m_events) {
                event.subscribed = false;
            }
        }
    }

    if (!available) {
        // The skeleton object stays alive on purpose: destroying it would zero the shared-memory
        // subscription control block underneath a peer that still holds a subscription.
        m_skeleton.StopOfferService();
        return;
    }

    auto const offered = m_skeleton.OfferService();
    if (!offered.has_value()) {
        score::mw::log::LogError()
            << kLog_tag << "Failed to offer the bridged service:" << offered.error();
        return;
    }

    // The interest callbacks that fired from within OfferService() already reconciled what they
    // saw. Catch up on everything they could not, and on what arrived before the offer.
    std::lock_guard const lock{m_mutex};
    for (std::size_t index = 0U; index < m_events.size(); ++index) {
        reconcile_subscription(static_cast<score::socom::Event_id>(index));
    }
}

void Provider_service_binding::on_peer_interest_change(score::socom::Event_id const event_id,
                                                       bool const wanted) noexcept {
    std::lock_guard const lock{m_mutex};
    if (event_id >= m_events.size()) {
        return;
    }
    m_events[event_id].wanted_by_peer = wanted;
    reconcile_subscription(event_id);
}

void Provider_service_binding::reconcile_subscription(
    score::socom::Event_id const event_id) noexcept {
    if (m_client_connector == nullptr) {
        // Still inside create_client_connector(), which reconciles again once it is done.
        return;
    }

    auto& event = m_events[event_id];
    bool const desired = m_service_available && event.wanted_by_peer;
    if (desired == event.subscribed) {
        return;
    }

    if (desired) {
        auto const subscribed =
            m_client_connector->subscribe_event(event_id, score::socom::Event_mode::update);
        if (!subscribed.has_value()) {
            score::mw::log::LogWarn() << kLog_tag << "Failed to subscribe to event" << event_id
                                      << ":" << subscribed.error();
            return;
        }
    } else {
        auto const unsubscribed = m_client_connector->unsubscribe_event(event_id);
        if (!unsubscribed.has_value()) {
            score::mw::log::LogWarn() << kLog_tag << "Failed to unsubscribe from event" << event_id
                                      << ":" << unsubscribed.error();
        }
    }
    event.subscribed = desired;
}

Result<score::socom::Writable_payload> Provider_service_binding::on_event_payload_allocate(
    score::socom::Event_id const event_id) noexcept {
    std::lock_guard const lock{m_mutex};

    if (event_id >= m_events.size()) {
        return MakeUnexpected(Mw_com_binding_error::logic_error_unknown_event);
    }
    auto& event = m_events[event_id];

    auto sample = event.skeleton_event->Allocate();
    if (!sample.has_value()) {
        // Back pressure: every slot is held by the peer or still in flight.
        score::mw::log::LogWarn() << kLog_tag << "Failed to allocate a sample for event" << event_id
                                  << ":" << sample.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_sample_allocation_failed);
    }

    auto* const sample_base = static_cast<std::byte*>(sample.value().Get());
    auto const slot_handle = m_allocations->insert(std::move(sample).value());

    return score::socom::Writable_payload{
        score::socom::Writable_payload::Writable_span{sample_base, event.addressed_sample_size},
        slot_handle,
        [allocations = std::weak_ptr<Allocations>{m_allocations}, slot_handle]() noexcept {
            // Runs when the producer drops the payload without sending it, and again, as a no-op,
            // after on_event_update() took the sample out. Weak, because the producer may outlive
            // the binding.
            auto const locked = allocations.lock();
            if (locked != nullptr) {
                (void)locked->take(slot_handle);
            }
        },
        event.header_size, kSample_prefix_size};
}

void Provider_service_binding::on_event_update(score::socom::Event_id const event_id,
                                               score::socom::Payload payload) noexcept {
    std::lock_guard const lock{m_mutex};

    if (event_id >= m_events.size()) {
        return;
    }

    auto sample = m_allocations->take(payload.get_slot_handle());
    if (!sample.has_value()) {
        // The payload was not allocated through this binding, so it is not backed by a LoLa slot
        // and there is nothing that could be sent.
        score::mw::log::LogWarn() << kLog_tag << "Dropping the update of event" << event_id
                                  << ": the payload is not backed by a mw::com sample";
        return;
    }

    auto const length = payload.data().size();
    if (length > kMax_payload_length) {
        score::mw::log::LogError() << kLog_tag << "Dropping the update of event" << event_id
                                   << ": payload length" << length << "is not representable";
        return;
    }
    write_payload_length(static_cast<std::byte*>(sample.value().Get()), length);

    auto const sent = m_events[event_id].skeleton_event->Send(std::move(sample).value());
    if (!sent.has_value()) {
        score::mw::log::LogWarn() << kLog_tag << "Failed to send event" << event_id << ":"
                                  << sent.error();
    }
}

}  // namespace score::gateway_ipc_binding::mw_com
