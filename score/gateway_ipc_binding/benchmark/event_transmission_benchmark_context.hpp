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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <score/assert.hpp>
#include <string>
#include <thread>

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/mw/com/runtime.h"
#include "score/socom/client_connector.hpp"
#include "score/socom/error.hpp"
#include "score/socom/runtime.hpp"
#include "score/socom/server_connector.hpp"

namespace score::gateway_ipc_binding {

using namespace std::chrono_literals;
using socom::Event_id;
using socom::Method_id;

inline void initialize_benchmark_runtime() {
    std::string manifest{"score/gateway_ipc_binding/benchmark/mw_com_config.json"};
    auto const* runfiles = std::getenv("TEST_SRCDIR");
    if (runfiles == nullptr) {
        runfiles = std::getenv("RUNFILES_DIR");
    }
    if (runfiles != nullptr) {
        auto const* workspace = std::getenv("TEST_WORKSPACE");
        manifest = std::string{runfiles} + "/" + (workspace != nullptr ? workspace : "_main") +
                   "/" + manifest;
    }
    score::mw::com::runtime::InitializeRuntime(
        score::mw::com::runtime::RuntimeConfiguration{score::filesystem::Path{manifest}});
}

class Event_transmission_benchmark_context final {
   public:
    explicit Event_transmission_benchmark_context(std::size_t payload_size)
        : runtime_server_{score::socom::create_runtime()},
          runtime_client_{score::socom::create_runtime()},
          payload_size_{payload_size} {
        SCORE_LANGUAGE_FUTURECPP_ASSERT(runtime_server_);
        SCORE_LANGUAGE_FUTURECPP_ASSERT(runtime_client_);

        create_gateway_pair();
        wait_for_gateway_connection();
        create_connectors();
        wait_for_service_available();
        subscribe_event();
    }

    Event_transmission_benchmark_context(Event_transmission_benchmark_context const&) = delete;
    Event_transmission_benchmark_context& operator=(Event_transmission_benchmark_context const&) =
        delete;
    Event_transmission_benchmark_context(Event_transmission_benchmark_context&&) = delete;
    Event_transmission_benchmark_context& operator=(Event_transmission_benchmark_context&&) =
        delete;

    ~Event_transmission_benchmark_context() {
        // Disconnect callback sources before mutex/condition_variable members are destroyed.
        sink_connector_.reset();
        source_connector_.reset();
        gateway_client_.reset();
        gateway_server_.reset();
    }

    [[nodiscard]] Result<std::chrono::nanoseconds> send_and_measure_once() {
        auto const seq = ++sequence_;
        Result<socom::Writable_payload> payload_result = MakeUnexpected(
            socom::Server_connector_error::runtime_error_no_client_subscribed_for_event);
        auto const deadline = std::chrono::steady_clock::now() + 10s;
        while (!payload_result) {
            payload_result = source_connector_->allocate_event_payload(event_id_);
            if (!payload_result && std::chrono::steady_clock::now() >= deadline) {
                return MakeUnexpected<std::chrono::nanoseconds>(payload_result.error());
            }
            // yield() is especially important for valgrind / massif when the benchmark is run with
            // a high iteration count, as the event processing thread may not get scheduled often
            // enough otherwise, causing the benchmark to run way longer.
            std::this_thread::yield();
        }

        auto payload = std::move(payload_result).value();
        auto writable = payload.wdata();
        if (writable.size() < sizeof(seq)) {
            return MakeUnexpected(socom::Error::runtime_error_request_rejected);
        }

        std::memcpy(writable.data(), &seq, sizeof(seq));

        auto const start = std::chrono::steady_clock::now();
        auto update_result = source_connector_->update_event(event_id_, std::move(payload));
        if (!update_result) {
            return MakeUnexpected<std::chrono::nanoseconds>(update_result.error());
        }

        std::unique_lock<std::mutex> lock{event_mutex_};
        if (!event_cv_.wait_for(
                lock, 10s, [this, seq]() noexcept { return last_received_sequence_ >= seq; })) {
            return MakeUnexpected<std::chrono::nanoseconds>(
                socom::Error::runtime_error_request_rejected);
        }
        return std::chrono::duration_cast<std::chrono::nanoseconds>(last_receive_time_ - start);
    }

   private:
    score::socom::Runtime::Uptr runtime_server_;
    score::socom::Runtime::Uptr runtime_client_;

    std::size_t payload_size_;

    score::socom::Service_interface_identifier const interface_{
        "bridged_com.test.gateway.benchmark", score::socom::Literal_tag{}, {1, 0}};
    score::socom::Service_instance const instance_{"instance1", score::socom::Literal_tag{}};
    score::socom::Server_service_interface_definition const server_interface_definition_{
        interface_, score::socom::to_num_of_methods(1), score::socom::to_num_of_events(1)};

    Event_id const event_id_{0};

    std::unique_ptr<Gateway_ipc_binding_server> gateway_server_;
    std::unique_ptr<Gateway_ipc_binding_client> gateway_client_;

    score::socom::Enabled_server_connector::Uptr source_connector_;
    score::socom::Client_connector::Uptr sink_connector_;

    std::mutex connection_state_mutex_;
    std::condition_variable connection_state_cv_;
    bool service_available_{false};
    bool event_subscribed_{false};

    std::atomic<std::uint64_t> sequence_{0};
    std::uint64_t last_received_sequence_{0};
    std::chrono::steady_clock::time_point last_receive_time_{};
    std::mutex event_mutex_;
    std::condition_variable event_cv_;

    void create_gateway_pair() {
        auto services = [this](mw_com::Role role) {
            return mw_com::Service_configs{
                mw_com::Service_config{interface_,
                                       instance_,
                                       "bridged_benchmark/event",
                                       6451U,
                                       1U,
                                       role,
                                       {{"event", mw_com::Mw_com_event_id{1U}, payload_size_}},
                                       mw_com::kNumber_of_sample_slots}};
        };
        gateway_server_ = mw_com::create_server(
            *runtime_server_, "benchmark/someipd",
            [services](score::cpp::span<std::byte const>,
                       mw_com::Add_bridged_services const& add_bridged_services) {
                return add_bridged_services(services(mw_com::Role::consumer));
            });
        SCORE_LANGUAGE_FUTURECPP_ASSERT(gateway_server_);

        gateway_client_ = mw_com::create_client(*runtime_client_, "benchmark/someipd",
                                                services(mw_com::Role::provider));
        SCORE_LANGUAGE_FUTURECPP_ASSERT(gateway_client_);

        auto start_result = gateway_server_->start();
        (void)start_result;  // Avoid unused variable warning in non-debug builds
        SCORE_LANGUAGE_FUTURECPP_ASSERT(start_result);
    }

    void wait_for_gateway_connection() {
        auto const deadline = std::chrono::steady_clock::now() + 10s;
        while (!gateway_client_->is_connected() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(1ms);
        }
        SCORE_LANGUAGE_FUTURECPP_ASSERT(gateway_client_->is_connected());
    }

    void create_connectors() {
        auto on_event_update = [this](score::socom::Client_connector const&, Event_id,
                                      score::socom::Payload payload) {
            std::uint64_t received_sequence = 0U;
            auto const data = payload.data();
            if (data.size() >= sizeof(received_sequence)) {
                std::memcpy(&received_sequence, data.data(), sizeof(received_sequence));
            }
            {
                std::lock_guard<std::mutex> lock{event_mutex_};
                last_received_sequence_ = received_sequence;
                last_receive_time_ = std::chrono::steady_clock::now();
            }
            event_cv_.notify_one();
        };

        score::socom::Client_connector::Callbacks client_callbacks{
            [this](score::socom::Client_connector const&, score::socom::Service_state state,
                   score::socom::Server_service_interface_definition const&) {
                if (state == score::socom::Service_state::available) {
                    {
                        std::lock_guard<std::mutex> lock{connection_state_mutex_};
                        service_available_ = true;
                    }
                    connection_state_cv_.notify_all();
                }
            },
            on_event_update, on_event_update,
            [](score::socom::Client_connector const&, Event_id) {
                return MakeUnexpected(score::socom::Error::runtime_error_request_rejected);
            }};

        auto sink_connector_result = runtime_server_->make_client_connector(
            server_interface_definition_, instance_, std::move(client_callbacks));
        SCORE_LANGUAGE_FUTURECPP_ASSERT(sink_connector_result);
        sink_connector_ = std::move(sink_connector_result).value();
        SCORE_LANGUAGE_FUTURECPP_ASSERT(sink_connector_);

        score::socom::Disabled_server_connector::Callbacks server_callbacks{
            [](score::socom::Enabled_server_connector&, Method_id, score::socom::Payload,
               score::socom::Method_call_reply_data_opt, score::socom::Posix_credentials const&) {
                return score::socom::Method_invocation::Uptr{};
            },
            [this](score::socom::Enabled_server_connector&, Event_id event_id,
                   score::socom::Event_state state) {
                if (event_id == event_id_ && state == score::socom::Event_state::subscribed) {
                    {
                        std::lock_guard<std::mutex> lock{connection_state_mutex_};
                        event_subscribed_ = true;
                    }
                    connection_state_cv_.notify_all();
                }
            },
            [](score::socom::Enabled_server_connector&, Event_id) {},
            [](score::socom::Enabled_server_connector&, Method_id) {
                return MakeUnexpected(score::socom::Error::runtime_error_request_rejected);
            }};

        auto disabled_connector_result = runtime_client_->make_server_connector(
            server_interface_definition_, instance_, std::move(server_callbacks));
        SCORE_LANGUAGE_FUTURECPP_ASSERT(disabled_connector_result);

        source_connector_ = score::socom::Disabled_server_connector::enable(
            std::move(disabled_connector_result).value());
        SCORE_LANGUAGE_FUTURECPP_ASSERT(source_connector_);
    }

    void wait_for_service_available() {
        std::unique_lock<std::mutex> lock{connection_state_mutex_};
        auto const available = connection_state_cv_.wait_for(
            lock, 10s, [this]() noexcept { return service_available_; });
        (void)available;  // Avoid unused variable warning in non-debug builds
        SCORE_LANGUAGE_FUTURECPP_ASSERT(available);
    }

    void subscribe_event() {
        auto const subscribe_result =
            sink_connector_->subscribe_event(event_id_, score::socom::Event_mode::update);
        (void)subscribe_result;  // Avoid unused variable warning in non-debug builds
        SCORE_LANGUAGE_FUTURECPP_ASSERT(subscribe_result);

        std::unique_lock<std::mutex> lock{connection_state_mutex_};
        auto const subscribed = connection_state_cv_.wait_for(
            lock, 10s, [this]() noexcept { return event_subscribed_; });
        (void)subscribed;  // Avoid unused variable warning in non-debug builds
        SCORE_LANGUAGE_FUTURECPP_ASSERT(subscribed);
    }
};

}  // namespace score::gateway_ipc_binding
