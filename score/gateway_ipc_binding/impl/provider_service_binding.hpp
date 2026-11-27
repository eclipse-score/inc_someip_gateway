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

#ifndef SCORE_GATEWAY_IPC_BINDING_IMPL_PROVIDER_SERVICE_BINDING_HPP
#define SCORE_GATEWAY_IPC_BINDING_IMPL_PROVIDER_SERVICE_BINDING_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

#include "score/mw/com/types.h"
#include "score/socom/client_connector.hpp"
#include "score/socom/payload.hpp"
#include "score/socom/runtime.hpp"
#include "service_binding.hpp"

namespace score::gateway_ipc_binding::mw_com {

/// \brief Provider half of a bridged service: consumes it via SOCom, offers it via mw::com.
///
/// \details Consumes a local SOCom service via `Client_connector` and publishes received events
///          through a `GenericSkeleton`:
///          - Availability: offers the skeleton while the local SOCom service is `available` and
///            withdraws it on `not_available` (recreating the skeleton across unsubscribed cycles).
///            Failed skeleton recreation or `OfferService()` rolls back cleanly so a subsequent
///            `not_available` is a safe no-op and a later `available` retries.
///          - Subscription: reconciles local SOCom availability and peer receive-handler interest
///            into `Client_connector::subscribe_event(Event_mode::update)` / `unsubscribe_event()`.
///          - Payload allocation & delivery: allocates LoLa slots tagged inline in
///            `Writable_payload` with the owning binding, offer/skeleton epoch, and
///            `socom::Event_id` (`logic_error_unknown_event` for out-of-range `event_id`;
///            `runtime_error_sample_allocation_failed` when the service is not offered,
///            `Allocate()` fails, or returns null). Event updates drop out-of-range `event_id`s,
///            updates arriving while unoffered, unallocated, foreign-binding, stale-epoch, or
///            cross-event payloads, and payloads exceeding `max_payload_size` without calling
///            `Send()`.
///          - Teardown: unsets skeleton handlers, destroys the SOCom connector, and withdraws any
///            active offer and destroys the skeleton on destruction.
class Provider_service_binding final : public Service_binding {
   public:
    /// \brief Creates the GenericSkeleton and SOCom client connector without offering the service.
    /// \pre `config.role == Role::provider` and `validate(config).has_value()`.
    /// \param runtime SOCom runtime used to create the client connector.
    /// \param config Validated bridged provider service instance configuration.
    /// \retval std::unique_ptr<Provider_service_binding> Created binding.
    /// \retval Mw_com_binding_error::logic_error_invalid_instance_specifier `instance_specifier`
    ///         is malformed.
    /// \retval Mw_com_binding_error::runtime_error_sample_size_mismatch A skeleton event's laid-out
    ///         sample size differs from `sample_size(event)`.
    /// \retval Mw_com_binding_error::runtime_error_service_setup_failed Skeleton creation, event
    ///         lookup, handler registration, or SOCom client connector creation failed.
    [[nodiscard]] static Result<std::unique_ptr<Provider_service_binding>> create(
        score::socom::Runtime& runtime, Service_config const& config) noexcept;

    ~Provider_service_binding() noexcept override;

    Provider_service_binding(Provider_service_binding const&) = delete;
    Provider_service_binding& operator=(Provider_service_binding const&) = delete;
    Provider_service_binding(Provider_service_binding&&) = delete;
    Provider_service_binding& operator=(Provider_service_binding&&) = delete;

    [[nodiscard]] Result<void> start() noexcept override { return {}; }

   private:
    /// \brief Ownership tag and LoLa sample slot stored inline in an allocated `Writable_payload`.
    struct Allocated_sample {
        Provider_service_binding const* owner;
        std::uint64_t skeleton_epoch;
        score::socom::Event_id event_id;
        score::mw::com::SampleAllocateePtr<void> sample;
    };
    static_assert(std::is_nothrow_move_constructible_v<Allocated_sample>);
    static_assert(std::is_nothrow_destructible_v<Allocated_sample>);

    /// \brief Per-event state, indexed by socom::Event_id
    struct Event {
        /// \brief Owned by m_skeleton, nullptr while the skeleton is torn down
        score::mw::com::GenericSkeletonEvent* skeleton_event{nullptr};
        std::size_t max_payload_size{0U};
        std::size_t addressed_sample_size{0U};
        /// \brief The peer registered a receive handler, i.e. it wants this event
        bool wanted_by_peer{false};
        /// \brief The binding currently holds a SOCom subscription for this event
        bool subscribed{false};
    };

    explicit Provider_service_binding(Service_config config) noexcept;

    /// \brief Create #m_skeleton from #m_config, then resolve its events and register handlers
    /// \details Used both by create() and to recreate a skeleton that was destroyed while no peer
    ///          held a subscription, see on_service_state_change().
    [[nodiscard]] Result<void> create_skeleton() noexcept;

    /// \brief Create the SOCom client connector that consumes the local service
    [[nodiscard]] Result<void> create_client_connector(score::socom::Runtime& runtime,
                                                       Service_config const& config) noexcept;

    void on_service_state_change(score::socom::Service_state state) noexcept;
    [[nodiscard]] Result<score::socom::Writable_payload> on_event_payload_allocate(
        score::socom::Event_id event_id) noexcept;
    void on_event_update(score::socom::Event_id event_id, score::socom::Payload payload) noexcept;
    void on_peer_interest_change(score::socom::Event_id event_id, bool wanted) noexcept;

    /// \brief Bring the SOCom subscription of one event in line with availability and interest
    /// \pre m_mutex is held
    void reconcile_subscription(score::socom::Event_id event_id) noexcept;

    /// \brief Serializes offering, withdrawing, and recreating the mw::com skeleton
    /// \details `OfferService()` and `StopOfferService()` take the mw::com event notification
    ///          registry lock, and mw::com holds that very lock while it calls
    ///          `on_peer_interest_change()`, which needs #m_mutex. Offering under #m_mutex would
    ///          therefore close a lock cycle with the mw::com message passing thread. This second
    ///          mutex keeps the transitions serialized instead, and is deliberately never taken
    ///          by `on_peer_interest_change()`. Lock order is m_offer_mutex before #m_mutex.
    ///
    ///          Recursive for the same reason as #m_mutex: `reconcile_subscription()` calls into
    ///          SOCom, which can call this object's callbacks back on the same thread.
    std::recursive_mutex m_offer_mutex;

    /// \brief Recursive because SOCom calls binding callbacks synchronously from binding calls,
    ///        e.g. `subscribe_event()` can end in `on_event_update()` on the same thread.
    std::recursive_mutex m_mutex;
    /// \brief Configuration this binding was created from, kept to recreate #m_skeleton
    Service_config m_config;
    /// \brief Absent while no local SOCom service is available and no peer holds a subscription
    std::optional<score::mw::com::GenericSkeleton> m_skeleton;
    std::vector<Event> m_events;
    std::uint64_t m_skeleton_epoch{0U};
    bool m_service_available{false};
    /// \brief Rejects state and peer-interest callbacks once destruction begins
    bool m_stopping{false};
    score::socom::Client_connector::Uptr m_client_connector{};
};

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_IMPL_PROVIDER_SERVICE_BINDING_HPP
