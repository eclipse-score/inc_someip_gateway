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

#ifndef SCORE_GATEWAY_IPC_BINDING_TEST_MW_COM_BRIDGED_SERVICE_APPS_HPP
#define SCORE_GATEWAY_IPC_BINDING_TEST_MW_COM_BRIDGED_SERVICE_APPS_HPP

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

#include "score/socom/client_connector.hpp"
#include "score/socom/runtime.hpp"
#include "score/socom/server_connector.hpp"

namespace score::gateway_ipc_binding::mw_com::test {

/// \brief How long a test waits for an expected asynchronous state change before giving up
constexpr auto kApp_timeout = std::chrono::seconds{10};

/// \brief How long a test waits before concluding that a state change does *not* happen
/// \details Only used for negative assertions, where waiting the full kApp_timeout would make
///          the test needlessly slow. It can only produce false negatives, never false passes.
constexpr auto kApp_negative_timeout = std::chrono::milliseconds{300};

/// \brief One received event update, copied out of the payload before it is released
struct Received_event {
    score::socom::Event_id event_id;
    std::vector<std::byte> header;
    std::vector<std::byte> data;
};

namespace detail {

/// \brief Block until `predicate` holds or `timeout` elapses
template <typename Predicate>
bool wait_for(std::mutex& mutex, std::condition_variable& condition, Predicate const& predicate,
              std::chrono::nanoseconds const timeout) {
    std::unique_lock lock{mutex};
    return condition.wait_for(lock, timeout, predicate);
}

inline std::vector<std::byte> to_vector(score::socom::Payload::Span const span) {
    return std::vector<std::byte>{span.begin(), span.end()};
}

}  // namespace detail

/// \brief Stand-in for the application that provides a SOME/IP service locally.
/// \details It is a plain SOCom server. The provider-role binding consumes it and pushes what it
///          receives into its GenericSkeleton, so from this class' point of view nothing about
///          the gateway is visible.
class Producer_app {
   public:
    Producer_app(score::socom::Runtime& runtime,
                 score::socom::Service_interface_identifier const& interface,
                 score::socom::Service_instance const& instance, std::size_t const num_events)
        : m_subscribed(num_events, false) {
        score::socom::Disabled_server_connector::Callbacks callbacks{
            [](score::socom::Enabled_server_connector&, score::socom::Method_id,
               score::socom::Payload, score::socom::Method_call_reply_data_opt,
               score::socom::Posix_credentials const&) -> score::socom::Method_invocation::Uptr {
                return nullptr;
            },
            [this](score::socom::Enabled_server_connector&, score::socom::Event_id const event_id,
                   score::socom::Event_state const state) {
                {
                    std::lock_guard const lock{m_mutex};
                    if (event_id < m_subscribed.size()) {
                        m_subscribed[event_id] = state == score::socom::Event_state::subscribed;
                    }
                }
                m_condition.notify_all();
            },
            [](score::socom::Enabled_server_connector&, score::socom::Event_id) {},
            [](score::socom::Enabled_server_connector&,
               score::socom::Method_id) -> Result<score::socom::Writable_payload> {
                return score::socom::Writable_payload{
                    score::socom::Writable_payload::Writable_span{}, score::socom::kNoSlotHandle,
                    []() {}};
            }};

        score::socom::Server_service_interface_definition const definition{
            interface, score::socom::to_num_of_methods(0U),
            score::socom::to_num_of_events(num_events)};

        auto connector = runtime.make_server_connector(definition, instance, std::move(callbacks));
        if (connector.has_value()) {
            m_connector =
                score::socom::Disabled_server_connector::enable(std::move(connector).value());
        }
    }

    ~Producer_app() = default;

    Producer_app(Producer_app const&) = delete;
    Producer_app& operator=(Producer_app const&) = delete;
    Producer_app(Producer_app&&) = delete;
    Producer_app& operator=(Producer_app&&) = delete;

    [[nodiscard]] bool is_valid() const noexcept { return m_connector != nullptr; }

    /// \brief Wait until the gateway has propagated the peer's interest in this event
    /// \param event_id Event to observe
    /// \param expected Subscription state to wait for
    /// \param timeout Use kApp_negative_timeout when `expected` is not supposed to be reached
    [[nodiscard]] bool wait_for_subscription(
        score::socom::Event_id const event_id, bool const expected,
        std::chrono::nanoseconds const timeout = kApp_timeout) {
        return detail::wait_for(
            m_mutex, m_condition,
            [this, event_id, expected]() {
                return (event_id < m_subscribed.size()) && (m_subscribed[event_id] == expected);
            },
            timeout);
    }

    /// \brief Allocate a payload, fill header and data, and publish it
    /// \return False if the payload could not be allocated or the update failed
    [[nodiscard]] bool send(score::socom::Event_id const event_id,
                            std::vector<std::byte> const& header,
                            std::vector<std::byte> const& data) {
        auto payload = m_connector->allocate_event_payload(event_id);
        if (!payload.has_value()) {
            return false;
        }

        auto const header_span = payload->header();
        if (static_cast<std::size_t>(header_span.size()) != header.size()) {
            return false;
        }
        std::copy(header.begin(), header.end(), header_span.begin());

        auto const data_span = payload->wdata();
        if (static_cast<std::size_t>(data_span.size()) < data.size()) {
            return false;
        }
        std::copy(data.begin(), data.end(), data_span.begin());
        if (!payload->shrink(data.size())) {
            return false;
        }

        return m_connector->update_event(event_id, std::move(payload).value()).has_value();
    }

   private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::vector<bool> m_subscribed;
    score::socom::Enabled_server_connector::Uptr m_connector{};
};

/// \brief Stand-in for the application that consumes a SOME/IP service locally.
/// \details It is a plain SOCom client of the service the consumer-role binding offers.
class Consumer_app {
   public:
    Consumer_app(score::socom::Runtime& runtime,
                 score::socom::Service_interface_identifier const& interface,
                 score::socom::Service_instance const& instance) {
        score::socom::Client_connector::Callbacks callbacks{
            [this](score::socom::Client_connector const&, score::socom::Service_state const state,
                   score::socom::Server_service_interface_definition const&) {
                {
                    std::lock_guard const lock{m_mutex};
                    m_available = state == score::socom::Service_state::available;
                }
                m_condition.notify_all();
            },
            [this](score::socom::Client_connector const&, score::socom::Event_id const event_id,
                   score::socom::Payload payload) { store(event_id, std::move(payload)); },
            [this](score::socom::Client_connector const&, score::socom::Event_id const event_id,
                   score::socom::Payload payload) { store(event_id, std::move(payload)); },
            [](score::socom::Client_connector const&,
               score::socom::Event_id) -> Result<score::socom::Writable_payload> {
                return MakeUnexpected(score::socom::Error::runtime_error_service_not_available);
            }};

        auto connector = runtime.make_client_connector(
            score::socom::Service_interface_definition{interface}, instance, std::move(callbacks));
        if (connector.has_value()) {
            m_connector = std::move(connector).value();
        }
    }

    ~Consumer_app() = default;

    Consumer_app(Consumer_app const&) = delete;
    Consumer_app& operator=(Consumer_app const&) = delete;
    Consumer_app(Consumer_app&&) = delete;
    Consumer_app& operator=(Consumer_app&&) = delete;

    [[nodiscard]] bool is_valid() const noexcept { return m_connector != nullptr; }

    /// \brief Wait until the gateway reports the bridged service as available or gone
    /// \param expected Availability to wait for
    /// \param timeout Use kApp_negative_timeout when `expected` is not supposed to be reached
    [[nodiscard]] bool wait_for_availability(
        bool const expected, std::chrono::nanoseconds const timeout = kApp_timeout) {
        return detail::wait_for(
            m_mutex, m_condition, [this, expected]() { return m_available == expected; }, timeout);
    }

    [[nodiscard]] bool subscribe(score::socom::Event_id const event_id) {
        return m_connector->subscribe_event(event_id, score::socom::Event_mode::update).has_value();
    }

    [[nodiscard]] bool unsubscribe(score::socom::Event_id const event_id) {
        return m_connector->unsubscribe_event(event_id).has_value();
    }

    /// \brief Wait until at least `count` event updates have arrived
    /// \param count Number of updates to wait for
    /// \param timeout Use kApp_negative_timeout when no update is supposed to arrive
    [[nodiscard]] bool wait_for_events(std::size_t const count,
                                       std::chrono::nanoseconds const timeout = kApp_timeout) {
        return detail::wait_for(
            m_mutex, m_condition, [this, count]() { return m_received.size() >= count; }, timeout);
    }

    [[nodiscard]] std::vector<Received_event> take_received() {
        std::lock_guard const lock{m_mutex};
        return std::exchange(m_received, {});
    }

   private:
    void store(score::socom::Event_id const event_id, score::socom::Payload payload) {
        {
            std::lock_guard const lock{m_mutex};
            m_received.push_back(Received_event{event_id, detail::to_vector(payload.header()),
                                                detail::to_vector(payload.data())});
        }
        m_condition.notify_all();
    }

    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_available{false};
    std::vector<Received_event> m_received{};
    score::socom::Client_connector::Uptr m_connector{};
};

}  // namespace score::gateway_ipc_binding::mw_com::test

#endif  // SCORE_GATEWAY_IPC_BINDING_TEST_MW_COM_BRIDGED_SERVICE_APPS_HPP
