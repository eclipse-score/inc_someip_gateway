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

#ifndef SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_PROVIDER_SERVICE_BINDING_HPP
#define SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_PROVIDER_SERVICE_BINDING_HPP

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "score/mw/com/types.h"
#include "score/socom/client_connector.hpp"
#include "score/socom/payload.hpp"
#include "score/socom/runtime.hpp"
#include "service_binding.hpp"

namespace score::gateway_ipc_binding::mw_com {

/// \brief Provider half of a bridged service: consumes it via SOCom, offers it via mw::com.
///
/// \details This peer is the data source for the peer daemon. It is a SOCom `Client_connector` of
///          the locally offered SOCom service and pushes everything it receives into a
///          `GenericSkeleton`. See `docs/mw_com_binding.rst`, section "Role assignment".
///
///          Three things are propagated:
///
///          - availability: the skeleton is created eagerly but only offered while the local
///            SOCom service is available
///          - subscription: `SetReceiveHandlerRegistrationChangedHandler` on the peer's side maps
///            to `Client_connector::subscribe_event()`. Because the two inputs, local
///            availability and remote interest, can arrive in any order, they are kept as flags
///            and reconciled
///          - payload: `Allocate()` backs `on_event_payload_allocate`, `Send()` backs
///            `on_event_update`
class Provider_service_binding final : public Service_binding {
   public:
    /// \brief Create the skeleton and the SOCom client connector for one bridged service.
    /// \details The skeleton is not offered yet; offering follows the SOCom service state.
    /// \param runtime SOCom runtime used to create the client connector
    /// \param config Bridged service instance, must have `role == Role::provider`
    /// \return The binding, or the first error encountered
    [[nodiscard]] static Result<std::unique_ptr<Provider_service_binding>> create(
        score::socom::Runtime& runtime, Service_config const& config) noexcept;

    ~Provider_service_binding() noexcept override;

    Provider_service_binding(Provider_service_binding const&) = delete;
    Provider_service_binding& operator=(Provider_service_binding const&) = delete;
    Provider_service_binding(Provider_service_binding&&) = delete;
    Provider_service_binding& operator=(Provider_service_binding&&) = delete;

   private:
    /// \brief In-flight `Allocate()` results, keyed by the slot handle the binding mints.
    /// \details SOCom only hands a `Writable_payload` back to the binding, so the
    ///          `SampleAllocateePtr` has to be parked somewhere in between. It is held through a
    ///          `shared_ptr` and referenced weakly by the payload destructor, so that a payload
    ///          the producer drops after the binding is gone does not touch freed memory.
    class Allocations {
       public:
        /// \brief Park a sample and mint the slot handle that identifies it
        [[nodiscard]] std::size_t insert(score::mw::com::SampleAllocateePtr<void> sample) noexcept;

        /// \brief Take a parked sample out, if it is still there
        [[nodiscard]] std::optional<score::mw::com::SampleAllocateePtr<void>> take(
            std::size_t slot_handle) noexcept;

        /// \brief Release every parked sample, returning the slots to LoLa
        void clear() noexcept;

       private:
        std::mutex m_mutex;
        std::unordered_map<std::size_t, score::mw::com::SampleAllocateePtr<void>> m_in_flight;
        std::size_t m_next_slot_handle{0U};
    };

    /// \brief Per-event state, indexed by socom::Event_id
    struct Event {
        /// \brief Owned by m_skeleton, stable across moves of the skeleton
        score::mw::com::GenericSkeletonEvent* skeleton_event;
        std::size_t header_size;
        std::size_t addressed_sample_size;
        /// \brief The peer registered a receive handler, i.e. it wants this event
        bool wanted_by_peer{false};
        /// \brief The binding currently holds a SOCom subscription for this event
        bool subscribed{false};
    };

    explicit Provider_service_binding(score::mw::com::GenericSkeleton skeleton) noexcept;

    /// \brief Look the configured events up in the skeleton and check their sample size
    [[nodiscard]] Result<void> resolve_events(Service_config const& config) noexcept;

    /// \brief Register the mw::com receive-handler-registration callbacks
    [[nodiscard]] Result<void> register_subscription_handlers() noexcept;

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

    /// \brief Recursive because SOCom calls binding callbacks synchronously from binding calls,
    ///        e.g. `subscribe_event()` can end in `on_event_update()` on the same thread.
    std::recursive_mutex m_mutex;
    score::mw::com::GenericSkeleton m_skeleton;
    std::vector<Event> m_events;
    bool m_service_available{false};
    bool m_offered{false};
    std::shared_ptr<Allocations> m_allocations{std::make_shared<Allocations>()};
    score::socom::Client_connector::Uptr m_client_connector{};
};

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_PROVIDER_SERVICE_BINDING_HPP
