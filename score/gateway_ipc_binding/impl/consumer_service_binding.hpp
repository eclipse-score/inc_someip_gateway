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
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "score/concurrency/thread_pool.h"
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
///          - Drains received samples from `GetNewSamples()` under a per-event mutex, reads the
///            8-byte prefix, and forwards payloads up to `max_payload_size` via `update_event()`,
///            dropping null samples or samples whose encoded length exceeds `max_payload_size`.
///          - Stops discovery, unsets active proxy receive handlers, disables the SOCom connector,
///            unsubscribes those proxy events, and shuts down the reconcile worker on destruction.
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
        Event(std::string event_name, std::size_t const configured_payload_size,
              std::size_t const sample_count)
            : name{std::move(event_name)},
              configured_max_payload_size{configured_payload_size},
              max_sample_count{sample_count} {
            sample_batch.reserve(max_sample_count);
        }

        ~Event() = default;
        Event(Event const&) = delete;
        Event& operator=(Event const&) = delete;
        Event(Event&&) = delete;
        Event& operator=(Event&&) = delete;

        std::string const name;
        /// \brief Minimum payload size expected from config (set by gatewayd; 0 in someipd, which
        ///        does not know sizes up front).
        std::size_t const configured_max_payload_size;
        std::size_t const max_sample_count;
        /// \brief Actual max payload size read from the provider's GenericProxyEvent when the
        ///        proxy connects.
        std::size_t max_payload_size{0U};
        /// \brief Owned by m_proxy, nullptr while the peer's service is not available
        score::mw::com::GenericProxyEvent* proxy_event{nullptr};
        /// \brief Serializes receive-handler execution and GetNewSamples() for this event
        std::mutex rx_mutex{};
        /// \brief Pre-reserved buffer drained by GetNewSamples() before forwarding to SOCom
        std::vector<score::mw::com::SamplePtr<void>> sample_batch{};
        /// \brief A local application is subscribed to this event
        bool wanted_locally{false};
        /// \brief The binding currently holds a mw::com subscription for this event
        bool subscribed{false};
        /// \brief A thread is currently reconciling this event's mw::com subscription
        bool reconciling{false};
    };

    Consumer_service_binding(std::vector<Event_config> const& events, std::size_t max_sample_count,
                             std::string instance_specifier,
                             score::mw::com::InstanceSpecifier specifier) noexcept;

    /// \brief Create the SOCom server connector, left disabled
    [[nodiscard]] Result<void> create_server_connector(score::socom::Runtime& runtime,
                                                       Service_config const& config) noexcept;

    void on_find_service(
        score::mw::com::ServiceHandleContainer<score::mw::com::HandleType> handles) noexcept;

    /// \brief Build the proxy for a discovered handle and enable the SOCom connector
    void attach_to_peer(score::mw::com::HandleType handle, std::uint64_t expected_epoch) noexcept;

    /// \brief Resolve every configured event on `proxy` and validate its peer sample size
    /// \return Resolved `(proxy_event, max_payload_size)` per event, or `std::nullopt` on mismatch
    [[nodiscard]] std::optional<
        std::vector<std::pair<score::mw::com::GenericProxyEvent*, std::size_t>>>
    resolve_proxy_events(score::mw::com::GenericProxy const& proxy) const noexcept;

    /// \brief Return whether an event should hold an active mw::com subscription right now
    /// \pre m_mutex is held
    [[nodiscard]] bool is_subscription_desired(Event const& event) const noexcept;

    /// \brief Return whether received samples for an event should be read and forwarded right now
    /// \pre m_mutex is held
    [[nodiscard]] bool can_receive_samples(Event const& event) const noexcept;

    /// \brief Tear the enabled connector and the proxy down
    /// \details Also used by the destructor, hence the separate function.
    void detach_from_peer() noexcept;

    /// \brief Detach subscribed events and the enabled connector while #m_lifecycle_mutex is held,
    ///        returning any active proxy so the caller destroys it after releasing binding locks
    /// \pre m_lifecycle_mutex is held
    [[nodiscard]] std::optional<score::mw::com::GenericProxy> detach_from_peer_locked() noexcept;

    void on_event_subscription_change(score::socom::Event_id event_id, bool subscribed) noexcept;
    void on_samples_available(score::socom::Event_id event_id) noexcept;

    /// \brief Bring the mw::com subscription of one event in line with the local demand
    /// \details Subscribing is deferred until the SOCom connector is enabled, because subscription
    ///          callbacks can fire from within `enable()` itself. `Subscribe()`, `Unsubscribe()`,
    ///          `SetReceiveHandler()`, and `UnsetReceiveHandler()` run with #m_mutex unlocked;
    ///          per-event #Event::reconciling serializes transitions for the same event.
    /// \pre #m_mutex is not held, this thread holds no #Event::rx_mutex, and this thread is not
    ///      executing a mw::com receive handler
    void reconcile_subscription(score::socom::Event_id event_id) noexcept;

    /// \brief Serializes `attach_to_peer()` against `detach_from_peer()`
    /// \details Held across `Disabled_server_connector::enable()`, initial
    ///          `reconcile_subscription()` in `attach_to_peer()`,
    ///          `Enabled_server_connector::disable()`, and teardown `UnsetReceiveHandler()` /
    ///          `Unsubscribe()`. SOCom holds its connector locks while invoking subscription
    ///          callbacks, so no callback or receive handler ever takes this mutex. Never held
    ///          across `GenericProxy` construction or destruction.
    std::mutex m_lifecycle_mutex;

    /// \brief Leaf mutex guarding binding state shared by discovery, SOCom callbacks, and receive
    ///        handlers
    /// \details Never held while acquiring another mutex or calling into mw::com or SOCom. Lock
    ///          order is #m_lifecycle_mutex before #m_mutex, and #Event::rx_mutex before #m_mutex.
    ///          Neither #m_lifecycle_mutex nor #m_mutex may be held while a `GenericProxy` is
    ///          created or destroyed because mw::com holds its service discovery lock across
    ///          `on_find_service()`.
    std::mutex m_mutex;
    std::deque<Event> m_events{};
    std::string m_instance_specifier;
    score::mw::com::InstanceSpecifier m_specifier;
    bool m_started{false};
    bool m_stopping{false};
    bool m_detaching{false};
    std::uint64_t m_discovery_epoch{0U};
    std::optional<score::mw::com::FindServiceHandle> m_find_handle{};
    std::optional<score::mw::com::HandleType> m_active_handle{};
    std::optional<score::mw::com::GenericProxy> m_proxy{};
    /// \brief Exactly one of the two is set except while enabling, detaching, or destroying
    score::socom::Disabled_server_connector::Uptr m_disabled_connector{};
    score::socom::Enabled_server_connector::Uptr m_enabled_connector{};
    /// \brief Notified under #m_mutex when an #Event::reconciling flag clears
    std::condition_variable m_reconcile_cv{};
    /// \brief Runs subscription reconciliation off the LoLa receive-handler thread when a SOCom
    ///        callback fires synchronously inside `update_event()`
    score::concurrency::ThreadPool m_reconcile_worker{1U, "ipc_reconcile"};
};

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_IMPL_CONSUMER_SERVICE_BINDING_HPP
