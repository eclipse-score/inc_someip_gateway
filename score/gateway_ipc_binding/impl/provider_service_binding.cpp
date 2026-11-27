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

Provider_service_binding::Provider_service_binding(Service_config config) noexcept
    : m_config{std::move(config)} {
    m_events.reserve(m_config.events.size());
    for (auto const& event : m_config.events) {
        m_events.push_back(Event{nullptr, event.max_payload_size, addressed_sample_size(event)});
    }
}

Provider_service_binding::~Provider_service_binding() noexcept {
    // Teardown order matters: LoLa terminates the process if a SampleAllocateePtr outlives its
    // event. See score/gateway_ipc_binding/docs/architecture/index.rst, section "Teardown".

    // 1. Detach and stop the mw::com callbacks. Hold m_offer_mutex across unsetting so a
    //    concurrent on_service_state_change() cannot destroy or recreate m_skeleton, but release
    //    both m_mutex (needed by running mw::com handlers) and m_offer_mutex before resetting
    //    m_client_connector in step 2.
    {
        std::lock_guard const offer_lock{m_offer_mutex};
        std::vector<score::mw::com::GenericSkeletonEvent*> events_to_unset{};
        {
            std::lock_guard const lock{m_mutex};
            m_stopping = true;
            events_to_unset.reserve(m_events.size());
            for (auto& event : m_events) {
                if (event.skeleton_event != nullptr) {
                    events_to_unset.push_back(event.skeleton_event);
                    event.skeleton_event = nullptr;
                }
            }
        }
        for (auto* const skeleton_event : events_to_unset) {
            auto const unset = skeleton_event->UnsetReceiveHandlerRegistrationChangedHandler();
            if (!unset.has_value()) {
                score::mw::log::LogWarn()
                    << kLog_tag
                    << "Failed to unset the receive handler registration handler:" << unset.error();
            }
        }
    }

    // 2. Stop the SOCom callbacks. The destructor blocks until running callbacks are done, so it
    //    must not run while this object's locks are held.
    score::socom::Client_connector::Uptr connector{};
    {
        std::lock_guard const lock{m_mutex};
        connector = std::move(m_client_connector);
    }
    connector.reset();

    // 3. Withdraw any active offer and destroy the skeleton outside m_mutex.
    bool was_offered{false};
    std::optional<score::mw::com::GenericSkeleton> skeleton_to_destroy{};
    {
        std::lock_guard const offer_lock{m_offer_mutex};
        std::lock_guard const lock{m_mutex};
        was_offered = m_service_available;
        m_service_available = false;
        ++m_skeleton_epoch;
        std::swap(skeleton_to_destroy, m_skeleton);
    }
    if (was_offered && skeleton_to_destroy.has_value()) {
        skeleton_to_destroy->StopOfferService();
    }
    skeleton_to_destroy.reset();
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

    // Validate event presence and laid-out sample sizes before committing m_skeleton.
    {
        auto skeleton_events = skeleton->GetEvents();
        for (auto const& event : m_config.events) {
            auto const entry = skeleton_events.find(event.name);
            if (entry == skeleton_events.cend()) {
                score::mw::log::LogError()
                    << kLog_tag << "Event" << event.name << "of" << m_config.instance_specifier
                    << "is missing from the GenericSkeleton";
                return MakeUnexpected(Mw_com_binding_error::runtime_error_service_setup_failed);
            }
            auto const size_info = entry->second.GetSizeInfo();
            if (size_info.size != sample_size(event)) {
                score::mw::log::LogError()
                    << kLog_tag << "Event" << event.name << "of" << m_config.instance_specifier
                    << "has sample size" << size_info.size << "but" << sample_size(event)
                    << "was configured";
                return MakeUnexpected(Mw_com_binding_error::runtime_error_sample_size_mismatch);
            }
        }
    }

    {
        std::lock_guard const lock{m_mutex};
        ++m_skeleton_epoch;
        m_skeleton.emplace(std::move(skeleton).value());
    }

    std::vector<score::mw::com::GenericSkeletonEvent*> resolved_events(m_config.events.size(),
                                                                       nullptr);
    {
        auto skeleton_events = m_skeleton->GetEvents();
        for (std::size_t index = 0U; index < m_config.events.size(); ++index) {
            auto const entry = skeleton_events.find(m_config.events[index].name);
            resolved_events[index] = &entry->second;
        }
    }

    for (std::size_t index = 0U; index < resolved_events.size(); ++index) {
        auto const event_id = static_cast<score::socom::Event_id>(index);
        auto const registered = resolved_events[index]->SetReceiveHandlerRegistrationChangedHandler(
            [this, event_id](bool const wanted) noexcept {
                on_peer_interest_change(event_id, wanted);
            });
        if (!registered.has_value()) {
            score::mw::log::LogError()
                << kLog_tag << "Failed to register the receive handler registration handler:"
                << registered.error();
            for (std::size_t prev = 0U; prev < index; ++prev) {
                (void)resolved_events[prev]->UnsetReceiveHandlerRegistrationChangedHandler();
            }
            std::optional<score::mw::com::GenericSkeleton> failed_skeleton{};
            {
                std::lock_guard const lock{m_mutex};
                std::swap(failed_skeleton, m_skeleton);
            }
            failed_skeleton.reset();
            return MakeUnexpected(Mw_com_binding_error::runtime_error_service_setup_failed);
        }
    }

    {
        std::lock_guard const lock{m_mutex};
        for (std::size_t index = 0U; index < m_events.size(); ++index) {
            m_events[index].skeleton_event = resolved_events[index];
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

    if (!available) {
        {
            std::lock_guard const lock{m_mutex};
            if (m_stopping || !m_service_available) {
                return;
            }
            m_service_available = false;
            ++m_skeleton_epoch;
            // SOCom drops all subscriptions when the service leaves the available state, so the
            // flags are cleared without calling SOCom back.
            for (auto& event : m_events) {
                event.subscribed = false;
            }
        }

        if (!m_skeleton.has_value()) {
            return;
        }
        m_skeleton->StopOfferService();

        std::vector<score::mw::com::GenericSkeletonEvent*> events_to_unset{};
        {
            std::lock_guard const lock{m_mutex};
            bool const no_peer_holds_a_subscription =
                std::none_of(m_events.cbegin(), m_events.cend(),
                             [](Event const& event) { return event.wanted_by_peer; });
            if (!no_peer_holds_a_subscription) {
                // Keep the skeleton alive while any peer still holds a subscription so the
                // shared-memory subscription control block stays valid.
                return;
            }
            events_to_unset.reserve(m_events.size());
            for (auto& event : m_events) {
                if (event.skeleton_event != nullptr) {
                    events_to_unset.push_back(event.skeleton_event);
                    event.skeleton_event = nullptr;
                }
            }
        }

        for (auto* const skeleton_event : events_to_unset) {
            auto const unset = skeleton_event->UnsetReceiveHandlerRegistrationChangedHandler();
            if (!unset.has_value()) {
                score::mw::log::LogWarn()
                    << kLog_tag
                    << "Failed to unset the receive handler registration handler:" << unset.error();
            }
        }

        // Move m_skeleton out under m_mutex so ~GenericSkeleton() runs outside m_mutex after
        // every in-flight operation under m_mutex has finished.
        std::optional<score::mw::com::GenericSkeleton> skeleton_to_destroy{};
        {
            std::lock_guard const lock{m_mutex};
            std::swap(skeleton_to_destroy, m_skeleton);
        }
        skeleton_to_destroy.reset();
        return;
    }

    bool has_skeleton{false};
    {
        std::lock_guard const lock{m_mutex};
        if (m_stopping || m_service_available) {
            return;
        }
        has_skeleton = m_skeleton.has_value();
    }

    if (!has_skeleton) {
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

    // Commit availability only after skeleton setup and OfferService() both succeeded, then catch
    // up on peer interest that arrived before or during the offer.
    std::lock_guard const lock{m_mutex};
    m_service_available = true;
    for (std::size_t index = 0U; index < m_events.size(); ++index) {
        reconcile_subscription(static_cast<score::socom::Event_id>(index));
    }
}

void Provider_service_binding::on_peer_interest_change(score::socom::Event_id const event_id,
                                                       bool const wanted) noexcept {
    std::lock_guard const lock{m_mutex};
    if (m_stopping || (event_id >= m_events.size())) {
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
    if (!m_service_available || (event.skeleton_event == nullptr)) {
        return MakeUnexpected(Mw_com_binding_error::runtime_error_sample_allocation_failed);
    }

    auto sample = event.skeleton_event->Allocate();
    if (!sample.has_value()) {
        // Back pressure: every slot is held by the peer or still in flight.
        score::mw::log::LogWarn() << kLog_tag << "Failed to allocate a sample for event" << event_id
                                  << ":" << sample.error();
        return MakeUnexpected(Mw_com_binding_error::runtime_error_sample_allocation_failed);
    }

    auto* const sample_base = static_cast<std::byte*>(sample.value().Get());
    if (sample_base == nullptr) {
        score::mw::log::LogError()
            << kLog_tag << "Allocated a null sample pointer for event" << event_id;
        return MakeUnexpected(Mw_com_binding_error::runtime_error_sample_allocation_failed);
    }

    return score::socom::Writable_payload{
        score::socom::Writable_payload::Writable_span{sample_base, event.addressed_sample_size}
            .subspan(kSample_prefix_size),
        Allocated_sample{this, m_skeleton_epoch, event_id, std::move(sample).value()}};
}

void Provider_service_binding::on_event_update(score::socom::Event_id const event_id,
                                               score::socom::Payload payload) noexcept {
    std::lock_guard const lock{m_mutex};
    // Move into a local scoped inside m_mutex so any dropped SampleAllocateePtr is destroyed
    // before m_mutex releases, preventing a race against StopOfferService() / ~GenericSkeleton().
    auto consumed_payload = std::move(payload);

    if (event_id >= m_events.size()) {
        return;
    }

    auto const& event = m_events[event_id];
    if (!m_service_available || (event.skeleton_event == nullptr)) {
        return;
    }

    auto* const allocated = consumed_payload.get_user_data<Allocated_sample>();
    if ((allocated == nullptr) || (allocated->owner != this) ||
        (allocated->skeleton_epoch != m_skeleton_epoch) || (allocated->event_id != event_id) ||
        (allocated->sample.Get() == nullptr)) {
        score::mw::log::LogWarn() << kLog_tag << "Dropping the update of event" << event_id
                                  << ": the payload is not backed by a matching mw::com sample";
        return;
    }

    auto const length = consumed_payload.data().size();
    if ((length > event.max_payload_size) || (length > kMax_payload_length)) {
        score::mw::log::LogError()
            << kLog_tag << "Dropping the update of event" << event_id << ": payload length"
            << length << "exceeds maximum" << event.max_payload_size;
        return;
    }

    write_payload_length(static_cast<std::byte*>(allocated->sample.Get()), length);

    auto const sent = event.skeleton_event->Send(std::move(allocated->sample));
    if (!sent.has_value()) {
        score::mw::log::LogWarn() << kLog_tag << "Failed to send event" << event_id << ":"
                                  << sent.error();
    }
}

}  // namespace score::gateway_ipc_binding::mw_com
