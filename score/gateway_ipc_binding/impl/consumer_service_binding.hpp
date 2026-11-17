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

#ifndef SCORE_GATEWAY_IPC_BINDING_IMPL_CONSUMER_SERVICE_BINDING_HPP
#define SCORE_GATEWAY_IPC_BINDING_IMPL_CONSUMER_SERVICE_BINDING_HPP

#include <condition_variable>
#include <cstddef>
#include <cstdint>
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
/// \details Owns a `GenericProxy` and a SOCom `Server_connector` for the same service:
///          - Discovery handle transitions (serialized across concurrent notifications):
///            - Empty `handles` detaches from the active peer and disables the SOCom connector.
///            - Duplicate notification for the already-attached handle (`handles.front() ==
///              active_handle`) is ignored without disabling the connector or recreating the proxy.
///            - Notification with a distinct handle (`!(handles.front() == active_handle)`) while
///              attached detaches from the old handle first and then attaches to `handles.front()`.
///          - Enables the SOCom connector only while the peer's `GenericSkeleton` is offered and
///            every configured event exists on the proxy with
///            `peer_sample_size >= kSample_prefix_size + configured_max_payload_size`, adopting
///            `peer_sample_size - kSample_prefix_size` as `max_payload_size`.
///          - Propagates local SOCom event subscriptions (including those requested synchronously
///            inside `enable()`) to `GenericProxyEvent::Subscribe(max_sample_count)` and registers
///            a receive handler.
///          - Reads the 8-byte prefix on received samples and forwards payloads up to
///            `max_payload_size` via `update_event()`, dropping null samples or samples whose
///            encoded length exceeds `max_payload_size`.
///          - Stops discovery, disables the SOCom connector, and unsets/unsubscribes active proxy
///            events on destruction.
class Consumer_service_binding final : public Service_binding {
   public:
    /// \brief Creates the disabled SOCom server connector without starting mw::com discovery.
    /// \pre `config.role == Role::consumer` and `validate(config).has_value()`.
    /// \param runtime SOCom runtime used to create the server connector.
    /// \param config Validated bridged consumer service instance configuration.
    /// \retval std::unique_ptr<Consumer_service_binding> Created binding with disabled connector.
    /// \retval Mw_com_binding_error::logic_error_invalid_instance_specifier `instance_specifier`
    ///         is malformed or not deployed in the mw::com runtime.
    /// \retval Mw_com_binding_error::runtime_error_service_setup_failed Creating the SOCom server
    ///         connector failed.
    [[nodiscard]] static Result<std::unique_ptr<Consumer_service_binding>> create(
        score::socom::Runtime& runtime, Service_config const& config) noexcept;

    ~Consumer_service_binding() noexcept override;

    Consumer_service_binding(Consumer_service_binding const&) = delete;
    Consumer_service_binding& operator=(Consumer_service_binding const&) = delete;
    Consumer_service_binding(Consumer_service_binding&&) = delete;
    Consumer_service_binding& operator=(Consumer_service_binding&&) = delete;

    /// \brief Starts mw::com service discovery for the peer's GenericSkeleton.
    /// \details Discovery runs until this object is destroyed; the handler may fire before this
    ///          call returns.
    /// \retval void Discovery started.
    /// \retval Mw_com_binding_error::logic_error_already_started `start()` was already called.
    /// \retval Mw_com_binding_error::runtime_error_service_find_failed Starting discovery failed.
    [[nodiscard]] Result<void> start() noexcept override;

   private:
    /// \brief Per-event state, indexed by socom::Event_id
    struct Event {
        std::string name;
        /// \brief Minimum payload size expected from config (set by gatewayd; 0 in someipd, which
        ///        does not know sizes up front).
        std::size_t configured_max_payload_size;
        std::size_t max_sample_count;
        /// \brief Actual max payload size read from the provider's GenericProxyEvent when the
        ///        proxy connects.
        std::size_t max_payload_size{0U};
        /// \brief Owned by m_proxy, nullptr while the peer's service is not available
        score::mw::com::GenericProxyEvent* proxy_event{nullptr};
        /// \brief A local application is subscribed to this event
        bool wanted_locally{false};
        /// \brief The binding currently holds a mw::com subscription for this event
        bool subscribed{false};
    };

    Consumer_service_binding(std::vector<Event> events, std::string instance_specifier,
                             score::mw::com::InstanceSpecifier specifier) noexcept;

    /// \brief Create the SOCom server connector, left disabled
    [[nodiscard]] Result<void> create_server_connector(score::socom::Runtime& runtime,
                                                       Service_config const& config) noexcept;

    void on_find_service(
        score::mw::com::ServiceHandleContainer<score::mw::com::HandleType> handles) noexcept;

    /// \brief Build the proxy for a discovered handle and enable the SOCom connector
    void attach_to_peer(score::mw::com::HandleType handle, std::uint64_t expected_epoch) noexcept;

    /// \brief Point every configured event at the matching event of #m_proxy and read its
    ///        max_payload_size from the peer
    /// \details On failure all event pointers are cleared again, so that none of them outlives
    ///          the proxy the caller is about to drop.
    /// \pre m_subscription_mutex and m_mutex are held
    /// \return True if every configured event was found and is layout compatible
    [[nodiscard]] bool bind_proxy_events() noexcept;

    /// \brief Reset the proxy event pointer of every configured event
    /// \pre m_mutex is held
    void clear_proxy_events() noexcept;

    /// \brief Tear the enabled connector and the proxy down
    /// \details Also used by the destructor, hence the separate function.
    void detach_from_peer() noexcept;

    /// \brief Detach the enabled connector and subscribed events while #m_lifecycle_mutex is held,
    ///        returning any active proxy so the caller destroys it after releasing binding locks
    /// \pre m_lifecycle_mutex is held
    [[nodiscard]] std::optional<score::mw::com::GenericProxy> detach_from_peer_locked() noexcept;

    void on_event_subscription_change(score::socom::Event_id event_id, bool subscribed) noexcept;
    void on_samples_available(score::socom::Event_id event_id) noexcept;

    /// \brief Bring the mw::com subscription of one event in line with the local demand
    /// \details Subscribing is deferred until the SOCom connector is enabled, because the
    ///          subscription callbacks fire from within `enable()` itself.
    /// \pre m_subscription_mutex is held and m_mutex is not
    void reconcile_subscription(score::socom::Event_id event_id) noexcept;

    /// \brief Serializes `attach_to_peer()` against `detach_from_peer()`
    /// \details The outermost of the three mutexes, and the only one that may be held across
    ///          `Disabled_server_connector::enable()` and `Enabled_server_connector::disable()`.
    ///          Both take SOCom locks that SOCom in turn holds while it calls this object's
    ///          connector callbacks, so a mutex those callbacks take must not be held across
    ///          them. This one is deliberately never taken by any callback.
    ///
    ///          Recursive out of caution, like the other two: SOCom calls binding callbacks
    ///          synchronously from binding calls.
    std::recursive_mutex m_lifecycle_mutex;

    /// \brief Keeps the #Event::proxy_event pointers valid while they are used
    /// \details `Subscribe()`, `Unsubscribe()`, `SetReceiveHandler()` and
    ///          `UnsetReceiveHandler()` block until a running receive handler has finished. They
    ///          must therefore never run under #m_mutex, which that very handler needs, and are
    ///          called with only a raw `GenericProxyEvent*` in hand. This mutex keeps them
    ///          serialized against each other and against the teardown that invalidates those
    ///          pointers, and is deliberately never taken by the receive handler. Lock order is
    ///          #m_lifecycle_mutex before m_subscription_mutex before #m_mutex.
    std::recursive_mutex m_subscription_mutex;

    /// \brief Guards the state the receive handler and the SOCom callbacks share
    /// \details Recursive because SOCom calls binding callbacks synchronously from binding calls,
    ///          e.g. `update_event()` can end in `on_event_subscription_change()` on the same
    ///          thread.
    ///
    ///          None of the three mutexes may be held while a `GenericProxy` is created or
    ///          destroyed. Creating and destroying one both take the mw::com service discovery
    ///          lock, and mw::com holds that same lock while it calls the find-service handler
    ///          that ends in `on_find_service()`, which takes all three. Holding one across
    ///          proxy construction or destruction would close that cycle and deadlock the daemon
    ///          against the mw::com discovery thread.
    std::recursive_mutex m_mutex;
    std::vector<Event> m_events;
    std::string m_instance_specifier;
    score::mw::com::InstanceSpecifier m_specifier;
    bool m_started{false};
    bool m_stopping{false};
    std::uint64_t m_discovery_epoch{0U};
    std::optional<score::mw::com::FindServiceHandle> m_find_handle{};
    std::optional<score::mw::com::HandleType> m_active_handle{};
    std::optional<score::mw::com::GenericProxy> m_proxy{};
    /// \brief Exactly one of the two is set except while detaching from a peer
    score::socom::Disabled_server_connector::Uptr m_disabled_connector{};
    score::socom::Enabled_server_connector::Uptr m_enabled_connector{};
    /// \brief Count of active `update_event()` calls using #m_enabled_connector, guarded by
    ///        #m_mutex
    std::size_t m_in_flight_updates{0U};
    /// \brief Notified under #m_mutex when #m_in_flight_updates drops to zero
    std::condition_variable_any m_updates_drained_cv{};
};

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_IMPL_CONSUMER_SERVICE_BINDING_HPP
