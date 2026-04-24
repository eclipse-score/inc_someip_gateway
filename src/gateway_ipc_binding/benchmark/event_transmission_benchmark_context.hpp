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

#include <benchmark/benchmark.h>
#include <score/message_passing/service_protocol_config.h>
#include <score/message_passing/unix_domain/unix_domain_client_factory.h>
#include <score/message_passing/unix_domain/unix_domain_server_factory.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <score/gateway_ipc_binding/error.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_client.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <score/gateway_ipc_binding/shared_memory_slot_manager.hpp>
#include <score/socom/client_connector.hpp>
#include <score/socom/error.hpp>
#include <score/socom/runtime.hpp>
#include <score/socom/server_connector.hpp>
#include <string>
#include <thread>

namespace score::gateway_ipc_binding {

using namespace std::chrono_literals;

[[nodiscard]] inline std::string make_unique_name(char const* prefix) {
    auto const now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::string{prefix} + "_" + std::to_string(getpid()) + "_" + std::to_string(now);
}

[[nodiscard]] inline Shared_memory_metadata make_metadata(std::string const& path,
                                                          std::uint32_t slot_size,
                                                          std::uint32_t slot_count) {
    Shared_memory_metadata metadata{};
    auto result = fixed_string_from_string<Shared_memory_path>(path);
    assert(result && "Path should fit into fixed-size metadata path");
    metadata.slot_size = slot_size;
    metadata.slot_count = slot_count;
    metadata.path = *result;
    return metadata;
}

class Event_transmission_benchmark_context final {
   public:
    explicit Event_transmission_benchmark_context(std::uint32_t payload_size)
        : runtime_server_{score::socom::create_runtime()},
          runtime_client_{score::socom::create_runtime()},
          service_name_{make_unique_name("gw_ipc_event_bench")},
          protocol_config_{service_name_, k_max_message_size, k_max_message_size,
                           k_max_message_size},
          server_shm_metadata_{
              make_metadata(make_unique_name("/gw_server_bench"), payload_size, k_slot_count)},
          client_shm_metadata_{
              make_metadata(make_unique_name("/gw_client_bench"), payload_size, k_slot_count)} {
        assert(runtime_server_);
        assert(runtime_client_);

        create_gateway_pair();
        wait_for_gateway_connection();
        create_connectors();
        wait_for_service_available();
        subscribe_event();
    }

    ~Event_transmission_benchmark_context() {
        // Disconnect callback sources before mutex/condition_variable members are destroyed.
        sink_connector_.reset();
        source_connector_.reset();
        gateway_client_.reset();
        gateway_server_.reset();
    }

    [[nodiscard]] Result<std::chrono::nanoseconds> send_and_measure_once() {
        auto const seq = ++sequence_;
        Result<std::unique_ptr<socom::Writable_payload>> payload_result = MakeUnexpected(
            socom::Server_connector_error::runtime_error_no_client_subscribed_for_event);
        while (!payload_result) {
            payload_result = source_connector_->allocate_event_payload(event_id_);
            // yield() is especially important for valgrind / massif when the benchmark is run with
            // a high iteration count, as the event processing thread may not get scheduled often
            // enough otherwise, causing the benchmark to run way longer.
            std::this_thread::yield();
        }

        auto payload = std::move(payload_result).value();
        auto writable = payload->wdata();
        if (writable.size() < sizeof(seq)) {
            return MakeUnexpected(score::gateway_ipc_binding::Shared_memory_manager_error::
                                      runtime_error_shared_memory_allocation_failed);
        }

        std::memcpy(writable.data(), &seq, sizeof(seq));

        auto const start = std::chrono::steady_clock::now();
        auto update_result = source_connector_->update_event(event_id_, std::move(payload));
        if (!update_result) {
            return MakeUnexpected<std::chrono::nanoseconds>(update_result.error());
        }

        std::unique_lock<std::mutex> lock{event_mutex_};
        event_cv_.wait(lock, [this, seq]() noexcept { return last_received_sequence_ >= seq; });
        return std::chrono::duration_cast<std::chrono::nanoseconds>(last_receive_time_ - start);
    }

   private:
    static constexpr std::size_t k_max_message_size = 4096U;
    static constexpr std::uint32_t k_slot_count = 64U;

    score::socom::Runtime::Uptr runtime_server_;
    score::socom::Runtime::Uptr runtime_client_;

    std::string service_name_;

    score::message_passing::ServiceProtocolConfig protocol_config_;
    score::message_passing::IServerFactory::ServerConfig const server_config_{10, 10, 10};
    score::message_passing::IClientFactory::ClientConfig const client_config_{10, 10, false, false,
                                                                              false};

    score::socom::Service_interface_identifier const interface_{
        "com.test.gateway.benchmark", score::socom::Literal_tag{}, {1, 0}};
    score::socom::Service_instance const instance_{"instance1", score::socom::Literal_tag{}};
    score::socom::Server_service_interface_definition const server_interface_definition_{
        interface_, score::socom::to_num_of_methods(1), score::socom::to_num_of_events(1)};

    Event_id const event_id_{0};

    Shared_memory_metadata server_shm_metadata_;
    Shared_memory_metadata client_shm_metadata_;

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
        Shared_memory_manager_factory::Shared_memory_configuration const server_shm_config{
            {interface_, {{instance_, server_shm_metadata_}}}};
        Shared_memory_manager_factory::Shared_memory_configuration const client_shm_config{
            {interface_, {{instance_, client_shm_metadata_}}}};

        score::message_passing::UnixDomainServerFactory server_factory;
        auto ipc_server = server_factory.Create(protocol_config_, server_config_);
        assert(ipc_server);
        gateway_server_ = Gateway_ipc_binding_server::create(
            *runtime_server_, std::move(ipc_server),
            Shared_memory_manager_factory::create(server_shm_config),
            [](auto, auto const&, auto) {});
        assert(gateway_server_);

        score::message_passing::UnixDomainClientFactory client_factory;
        auto connection = client_factory.Create(protocol_config_, client_config_);
        gateway_client_ = Gateway_ipc_binding_client::create(
            *runtime_client_, std::move(connection),
            Shared_memory_manager_factory::create(client_shm_config));
        assert(gateway_client_);

        auto start_result = gateway_server_->start();
        (void)start_result;  // Avoid unused variable warning in non-debug builds
        assert(start_result);
    }

    void wait_for_gateway_connection() {
        while (!gateway_client_->is_connected()) {
            std::this_thread::sleep_for(1ms);
        }
    }

    void create_connectors() {
        auto on_event_update = [this](score::socom::Client_connector const&, Event_id,
                                      score::socom::Payload::Uptr payload) {
            std::uint64_t received_sequence = 0U;
            auto const data = payload->data();
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
        assert(sink_connector_result);
        sink_connector_ = std::move(sink_connector_result).value();
        assert(sink_connector_);

        score::socom::Disabled_server_connector::Callbacks server_callbacks{
            [](score::socom::Enabled_server_connector&, Method_id, score::socom::Payload::Uptr,
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
        assert(disabled_connector_result);

        source_connector_ = score::socom::Disabled_server_connector::enable(
            std::move(disabled_connector_result).value());
        assert(source_connector_);
    }

    void wait_for_service_available() {
        std::unique_lock<std::mutex> lock{connection_state_mutex_};
        auto const available = connection_state_cv_.wait_for(
            lock, 10s, [this]() noexcept { return service_available_; });
        (void)available;  // Avoid unused variable warning in non-debug builds
        assert(available);
    }

    void subscribe_event() {
        auto const subscribe_result =
            sink_connector_->subscribe_event(event_id_, score::socom::Event_mode::update);
        (void)subscribe_result;  // Avoid unused variable warning in non-debug builds
        assert(subscribe_result);

        std::unique_lock<std::mutex> lock{connection_state_mutex_};
        auto const subscribed = connection_state_cv_.wait_for(
            lock, 10s, [this]() noexcept { return event_subscribed_; });
        (void)subscribed;  // Avoid unused variable warning in non-debug builds
        assert(subscribed);
    }
};

}  // namespace score::gateway_ipc_binding
