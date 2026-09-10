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

#ifndef SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_CONSUMER_SERVICE_BINDING_HPP
#define SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_CONSUMER_SERVICE_BINDING_HPP

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "score/mw/com/types.h"
#include "score/socom/runtime.hpp"
#include "score/socom/server_connector.hpp"
#include "service_binding.hpp"

namespace score::gateway_ipc_binding::mw_com {

/// \brief Consumer half of a bridged service: receives it via mw::com, offers it via SOCom.
///
/// \details This peer is the data sink for the peer daemon. It owns a `GenericProxy` and is a
///          SOCom `Server_connector` for the same service. See `docs/mw_com_binding.rst`, section
///          "Role assignment".
///
///          The SOCom connector is created disabled and only enabled while the peer's
///          `GenericSkeleton` is offered, so that local applications see the service exactly as
///          long as the peer provides it. Local subscriptions are forwarded as
///          `GenericProxyEvent::Subscribe()` plus a receive handler, so no data is produced
///          across the link while nobody wants it.
class Consumer_service_binding final : public Service_binding {
   public:
    /// \brief Create the SOCom server connector and start mw::com service discovery.
    /// \details Discovery runs until this object is destroyed. The find-service handler may
    ///          already fire before this function returns.
    /// \param runtime SOCom runtime used to create the server connector
    /// \param config Bridged service instance, must have `role == Role::consumer`
    /// \return The binding, or the first error encountered
    [[nodiscard]] static Result<std::unique_ptr<Consumer_service_binding>> create(
        score::socom::Runtime& runtime, Service_config const& config) noexcept;

    ~Consumer_service_binding() noexcept override;

    Consumer_service_binding(Consumer_service_binding const&) = delete;
    Consumer_service_binding& operator=(Consumer_service_binding const&) = delete;
    Consumer_service_binding(Consumer_service_binding&&) = delete;
    Consumer_service_binding& operator=(Consumer_service_binding&&) = delete;

   private:
    /// \brief Per-event state, indexed by socom::Event_id
    struct Event {
        std::string name;
        std::size_t header_size;
        std::size_t max_payload_size;
        std::size_t max_sample_count;
        /// \brief Owned by m_proxy, nullptr while the peer's service is not available
        score::mw::com::GenericProxyEvent* proxy_event{nullptr};
        /// \brief A local application is subscribed to this event
        bool wanted_locally{false};
        /// \brief The binding currently holds a mw::com subscription for this event
        bool subscribed{false};
    };

    Consumer_service_binding(std::vector<Event> events, std::string instance_specifier) noexcept;

    /// \brief Create the SOCom server connector, left disabled
    [[nodiscard]] Result<void> create_server_connector(score::socom::Runtime& runtime,
                                                       Service_config const& config) noexcept;

    /// \brief Start mw::com service discovery for the peer's GenericSkeleton
    [[nodiscard]] Result<void> start_find_service(
        score::mw::com::InstanceSpecifier specifier) noexcept;

    void on_find_service(
        score::mw::com::ServiceHandleContainer<score::mw::com::HandleType> handles) noexcept;

    /// \brief Build the proxy for a discovered handle and enable the SOCom connector
    void attach_to_peer(score::mw::com::HandleType handle) noexcept;

    /// \brief Tear the enabled connector and the proxy down
    /// \details Also used by the destructor, hence the separate function.
    void detach_from_peer() noexcept;

    void on_event_subscription_change(score::socom::Event_id event_id, bool subscribed) noexcept;
    void on_samples_available(score::socom::Event_id event_id) noexcept;

    /// \brief Bring the mw::com subscription of one event in line with the local demand
    /// \details Subscribing is deferred until the SOCom connector is enabled, because the
    ///          subscription callbacks fire from within `enable()` itself.
    /// \pre m_lifecycle_mutex is held and m_mutex is not
    void reconcile_subscription(score::socom::Event_id event_id) noexcept;

    /// \brief Serializes everything that changes the proxy or its subscriptions
    /// \details `UnsetReceiveHandler()`, `Unsubscribe()` and destroying the `GenericProxy` all
    ///          block until a running receive handler has finished. They must therefore never run
    ///          under #m_mutex, which that very handler needs. This second mutex keeps them
    ///          serialized against each other instead, and is deliberately never taken by the
    ///          receive handler. Lock order is m_lifecycle_mutex before #m_mutex.
    ///
    ///          Recursive because `Disabled_server_connector::enable()` calls
    ///          `on_event_subscription_change()` synchronously from within `attach_to_peer()`.
    std::recursive_mutex m_lifecycle_mutex;

    /// \brief Guards the state the receive handler and the SOCom callbacks share
    /// \details Recursive because SOCom calls binding callbacks synchronously from binding calls,
    ///          e.g. `update_event()` can end in `on_event_subscription_change()` on the same
    ///          thread.
    std::recursive_mutex m_mutex;
    std::vector<Event> m_events;
    std::string m_instance_specifier;
    std::optional<score::mw::com::FindServiceHandle> m_find_handle{};
    std::optional<score::mw::com::GenericProxy> m_proxy{};
    /// \brief Exactly one of the two is set at any time
    score::socom::Disabled_server_connector::Uptr m_disabled_connector{};
    score::socom::Enabled_server_connector::Uptr m_enabled_connector{};
};

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_CONSUMER_SERVICE_BINDING_HPP
