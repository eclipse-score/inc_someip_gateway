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

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>
#include <variant>

#include "sample_layout.hpp"
#include "score/gateway_ipc_binding/error.hpp"
#include "score/mw/com/impl/configuration/lola_service_instance_deployment.h"
#include "score/mw/com/impl/instance_identifier.h"
#include "score/mw/com/impl/skeleton_base.h"
#include "score/mw/log/logging.h"

namespace score::gateway_ipc_binding::mw_com {

namespace {

/// \brief Log prefix shared by the whole binding
constexpr std::string_view kLog_tag{"[gateway_ipc_binding]"};

}  // namespace

void Provider_service_binding::Allocations::initialize(std::size_t const capacity) noexcept {
    std::lock_guard const lock{m_mutex};
    if (!m_entries.empty()) {
        return;
    }
    m_entries.resize(capacity);
    for (std::size_t index = 0U; index < capacity; ++index) {
        m_entries[index].handle = index;
        m_entries[index].next_free = index + 1U;
    }
}

std::optional<std::size_t> Provider_service_binding::Allocations::insert(
    score::mw::com::SampleAllocateePtr<void> sample) noexcept {
    std::lock_guard const lock{m_mutex};
    if (m_free_head == m_entries.size()) {
        return std::nullopt;
    }
    auto& entry = m_entries[m_free_head];
    m_free_head = entry.next_free;
    entry.sample.emplace(std::move(sample));
    return entry.handle;
}

std::optional<score::mw::com::SampleAllocateePtr<void>> Provider_service_binding::Allocations::take(
    std::size_t const slot_handle) noexcept {
    std::lock_guard const lock{m_mutex};
    if (m_entries.empty()) {
        return std::nullopt;
    }
    auto const index = slot_handle % m_entries.size();
    auto& entry = m_entries[index];
    if (!entry.sample.has_value() || entry.handle != slot_handle) {
        return std::nullopt;
    }
    auto sample = std::move(entry.sample);
    release(index);
    return sample;
}

void Provider_service_binding::Allocations::release(std::size_t const index) noexcept {
    auto& entry = m_entries[index];
    entry.sample.reset();
    if (entry.handle <= std::numeric_limits<std::size_t>::max() - m_entries.size()) {
        entry.handle += m_entries.size();
        entry.next_free = m_free_head;
        m_free_head = index;
    }
}

void Provider_service_binding::Allocations::clear() noexcept {
    std::lock_guard const lock{m_mutex};
    for (std::size_t index = 0U; index < m_entries.size(); ++index) {
        if (m_entries[index].sample.has_value()) {
            release(index);
        }
    }
}

Provider_service_binding::Provider_service_binding(Service_config config) noexcept
    : m_config{std::move(config)} {}

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

    // 3. Nothing calls back anymore. Release the slots, then the skeleton that owns them, if any.
    m_allocations->clear();
    if (m_skeleton.has_value()) {
        m_skeleton->StopOfferService();
    }
}

Result<std::unique_ptr<Provider_service_binding>> Provider_service_binding::create(
    score::socom::Runtime& runtime, Service_config const& config) noexcept {
    // Not make_unique: the constructor is private.
    std::unique_ptr<Provider_service_binding> binding{new Provider_service_binding{config}};

    auto created = binding->create_skeleton();
    if (!created.has_value()) {
        return MakeUnexpected<std::unique_ptr<Provider_service_binding>>(
            std::move(created).error());
    }

    auto connected = binding->create_client_connector(runtime, config);
    if (!connected.has_value()) {
        return MakeUnexpected<std::unique_ptr<Provider_service_binding>>(
            std::move(connected).error());
    }

    return binding;
}

Result<void> Provider_service_binding::create_skeleton() noexcept {
    auto specifier = make_instance_specifier(m_config.instance_specifier);
    if (!specifier.has_value()) {
        return MakeUnexpected<void>(std::move(specifier).error());
    }

    std::vector<score::mw::com::EventInfo> event_infos{};
    event_infos.reserve(m_config.events.size());
    for (auto const& event : m_config.events) {
        event_infos.push_back(score::mw::com::EventInfo{event.name, sample_meta_info(event)});
    }

    score::mw::com::GenericSkeletonServiceElementInfo element_info{};
    element_info.events = event_infos;

    auto skeleton = score::mw::com::GenericSkeleton::Create(specifier.value(), element_info);
    if (!skeleton.has_value()) {
        score::mw::log::LogError() << kLog_tag << "Failed to create the GenericSkeleton for"
                                   << m_config.instance_specifier << ":" << skeleton.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_service_setup_failed);
    }
    m_skeleton.emplace(std::move(skeleton).value());
    m_events.clear();

    auto resolved = resolve_events(m_config);
    if (!resolved.has_value()) {
        return resolved;
    }

    auto const identifier =
        score::mw::com::impl::SkeletonBaseView{*m_skeleton}.GetAssociatedInstanceIdentifier();
    auto const& deployment =
        score::mw::com::impl::InstanceIdentifierView{identifier}.GetServiceInstanceDeployment();
    auto const* const lola =
        std::get_if<score::mw::com::impl::LolaServiceInstanceDeployment>(&deployment.bindingInfo_);
    if (lola == nullptr) {
        return MakeUnexpected(Mw_com_binding_error::runtime_error_service_setup_failed);
    }
    std::size_t capacity{0U};
    for (auto const& event : m_config.events) {
        auto const entry = lola->events_.find(event.name);
        if (entry == lola->events_.end()) {
            return MakeUnexpected(Mw_com_binding_error::runtime_error_service_setup_failed);
        }
        auto const slots = entry->second.GetNumberOfSampleSlotsExcludingTracingSlot();
        if (!slots.has_value() ||
            slots.value() > std::numeric_limits<std::size_t>::max() - capacity) {
            return MakeUnexpected(Mw_com_binding_error::runtime_error_service_setup_failed);
        }
        capacity += slots.value();
    }
    m_allocations->initialize(capacity);

    return register_subscription_handlers();
}

Result<void> Provider_service_binding::resolve_events(Service_config const& config) noexcept {
    // Not const: the const find() overload would only hand out a const event.
    auto skeleton_events = m_skeleton->GetEvents();
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
        // otherwise a full-size payload would be written or read past the end of a sample.
        auto const size_info = entry->second.GetSizeInfo();
        if (size_info.size != sample_size(event)) {
            score::mw::log::LogError()
                << kLog_tag << "Event" << event.name << "of" << config.instance_specifier
                << "has sample size" << size_info.size << "but" << sample_size(event)
                << "was configured";
            return MakeUnexpected(Mw_com_binding_error::runtime_error_sample_size_mismatch);
        }

        m_events.push_back(Event{&entry->second, addressed_sample_size(event)});
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

    bool no_peer_holds_a_subscription{false};
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
            no_peer_holds_a_subscription =
                std::none_of(m_events.cbegin(), m_events.cend(),
                             [](Event const& event) { return event.wanted_by_peer; });
        }
    }

    if (!available) {
        m_allocations->clear();
        m_skeleton->StopOfferService();
        // TODO this needs further inspection if the skeleton can delete shared memory while proxies
        // might still access it
        if (no_peer_holds_a_subscription) {
            // No peer holds a reference into the shared memory, so the skeleton can be fully torn
            // down and recreated on the next offer instead of being re-offered as-is, which LoLa
            // does not support, see docs/mw_com_binding.rst, section "Known gaps". Withdraw the
            // offer first: skipping it would leave the peer's service discovery unaware that this
            // service went away. Same teardown order as the destructor otherwise: unset handlers
            // before releasing what they capture.
            for (auto& event : m_events) {
                auto const unset =
                    event.skeleton_event->UnsetReceiveHandlerRegistrationChangedHandler();
                if (!unset.has_value()) {
                    score::mw::log::LogWarn() << kLog_tag
                                              << "Failed to unset the receive handler "
                                                 "registration handler:"
                                              << unset.error();
                }
            }
            m_events.clear();
            m_skeleton.reset();
        } else {
            // The skeleton object stays alive on purpose: destroying it would zero the
            // shared-memory subscription control block underneath a peer that still holds a
            // subscription.
        }
        return;
    }

    if (!m_skeleton.has_value()) {
        auto const recreated = create_skeleton();
        if (!recreated.has_value()) {
            score::mw::log::LogError()
                << kLog_tag << "Failed to recreate the GenericSkeleton:" << recreated.error();
            return;
        }
    }

    auto const offered = m_skeleton->OfferService();
    if (!offered.has_value()) {
        score::mw::log::LogError()
            << kLog_tag << "Failed to offer the bridged service:" << offered.error();
        return;
    }

    // The interest callbacks that fired from within OfferService()/create_skeleton() already
    // reconciled what they saw. Catch up on everything they could not, and on what arrived before
    // the offer.
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
        return MakeUnexpected(Shared_memory_manager_error::runtime_error_no_available_slots);
    }

    auto* const sample_base = static_cast<std::byte*>(sample.value().Get());
    auto const inserted = m_allocations->insert(std::move(sample).value());
    if (!inserted.has_value()) {
        return MakeUnexpected(Shared_memory_manager_error::runtime_error_no_available_slots);
    }
    auto const slot_handle = inserted.value();

    return score::socom::Writable_payload{
        score::socom::Writable_payload::Writable_span{sample_base, event.addressed_sample_size}
            .subspan(kSample_prefix_size),
        slot_handle,
        [allocations = std::weak_ptr<Allocations>{m_allocations}, slot_handle]() noexcept {
            // Runs when the producer drops the payload without sending it, and again, as a no-op,
            // after on_event_update() took the sample out. Weak, because the producer may outlive
            // the binding.
            auto const locked = allocations.lock();
            if (locked != nullptr) {
                (void)locked->take(slot_handle);
            }
        }};
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
