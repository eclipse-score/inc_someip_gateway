/********************************************************************************
 * Copyright (c) 2025 Contributors to the Eclipse Foundation
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

#include <getopt.h>

#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
#include <score/socom/final_action.hpp>
#include <thread>

#include "local_service_instance.h"
#include "remote_service_instance.h"
#include "score/filesystem/path.h"
#include "score/gateway_ipc_binding/gateway_ipc_binding_client.hpp"
#include "score/message_passing/client_factory.h"
#include "score/message_passing/service_protocol_config.h"
#include "score/mw/com/runtime.h"
#include "score/mw/log/logging.h"
#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"
#include "src/config/mw_someip_config_generated.h"
#include "src/serializer/serializer.h"

// In the main file we are not in any namespace
using namespace score;
using namespace score::someip_gateway::gatewayd;

// Global flag to control application shutdown
static std::atomic<bool> shutdown_requested{false};

// Signal handler for graceful shutdown
void termination_handler(int /*signal*/) {
    std::cout << "Received termination signal. Initiating graceful shutdown..." << std::endl;
    shutdown_requested.store(true);
}

// Help text, showing usage syntax and available options
void print_help() {
    std::cout << "Syntax: gatewayd -h/--help\n"
              << "        gatewayd -c/--configuration <config.bin> "
              << "-s/--service_instance_manifest <manifest.json>\n"
              << "\n";

    std::cout << "Options:\n"
              << " -h/--help Displays this help\n"
              << " -c/--configuration Specifies the configuration file\n"
              << " -s/--service_instance_manifest Specifies the service instance manifest file\n"
              << "\n";
}

int main(int argc, char* argv[]) {
    // Register signal handlers for graceful shutdown
    std::signal(SIGTERM, termination_handler);
    std::signal(SIGINT, termination_handler);

    const char* const short_opts = "hc:s:";
    const option long_opts[] = {{"help", no_argument, nullptr, 'h'},
                                {"configuration", required_argument, nullptr, 'c'},
                                {"service_instance_manifest", required_argument, nullptr, 's'},
                                {nullptr, no_argument, nullptr, 0}};

    score::filesystem::Path service_instance_manifest_path{};
    score::filesystem::Path configuration_path{};

    while (true) {
        const int opt{getopt_long(argc, argv, short_opts, long_opts, nullptr)};
        if (opt == -1) {
            // No more options, break the while loop
            break;
        }
        switch (static_cast<char>(opt)) {
            case 'h': {
                print_help();
                return 0;
            }
            case 'c': {
                configuration_path = score::filesystem::Path{optarg};
                break;
            }
            case 's': {
                service_instance_manifest_path = score::filesystem::Path{optarg};
                break;
            }
            // Unknown option
            default: {
                print_help();
                return 1;
            }
        }
    }

    // Both configuration files are required, otherwise print help and exit
    if (configuration_path.Empty() || service_instance_manifest_path.Empty()) {
        print_help();
        return 1;
    }

    // Read config data
    // TODO: Use memory mapped file instead of copying into buffer
    std::ifstream config_file;
    config_file.open(configuration_path.CStr(), std::ios::binary | std::ios::in);

    if (!config_file.is_open()) {
        score::mw::log::LogFatal() << "Error: Could not open config file " << configuration_path;
        return 1;
    }

    config_file.seekg(0, std::ios::end);
    std::streampos length = config_file.tellg();

    if (length <= 0) {
        score::mw::log::LogFatal()
            << "Error: Invalid config file size: " << static_cast<std::size_t>(length);
        config_file.close();
        return 1;
    }

    config_file.seekg(0, std::ios::beg);
    auto config_buffer = std::shared_ptr<char>(new char[length]);
    config_file.read(config_buffer.get(), length);
    config_file.close();

    auto config = std::shared_ptr<const score::mw_someip_config::Root>(
        config_buffer, score::mw_someip_config::GetRoot(config_buffer.get()));

    // TODO: Align on which identifier to pass to the serializer
    if (score_com_serializer_init(configuration_path.Native().data(),
                                  configuration_path.Native().size()) !=
        score_com_serializer_result_ok) {
        score::mw::log::LogFatal() << "Error: Failed to initialize serializer plugin.";
        return 1;
    }

    score::socom::Final_action const serializer_cleanup{[]() {
        if (score_com_serializer_deinit() != score_com_serializer_result_ok) {
            score::mw::log::LogError() << "Warning: Failed to deinitialize serializer plugin.";
        }
    }};

    score::mw::com::runtime::InitializeRuntime(
        score::mw::com::runtime::RuntimeConfiguration{service_instance_manifest_path});

    // Create the SOCom runtime
    auto socom_runtime = socom::create_runtime();

    // "Connect" is the largest IPC message due to the embedded SHM metadata
    static_assert(sizeof(gateway_ipc_binding::Connect{}) <= score::someip::kMaxIpcMessageSize,
                  "Connect message exceeds max_send_size");

    message_passing::ServiceProtocolConfig proto_config{
        "someipd_gatewayd_ipc", score::someip::kMaxIpcMessageSize,
        score::someip::kMaxIpcMessageSize, score::someip::kMaxIpcMessageSize};

    auto ipc_connection =
        message_passing::ClientFactory{}.Create(proto_config, {10, 10, false, false, false});

    // Build shared memory configuration
    // TODO: figure out why both server and client config is required, otherwise CRASH
    gateway_ipc_binding::Shared_memory_manager_factory::Shared_memory_configuration shm_config;
    gateway_ipc_binding::Shared_memory_manager_factory::Shared_memory_configuration
        server_shm_config;
    for (auto service_type_config : *config->service_types()) {
        socom::Service_interface_identifier const iface{
            service_type_config->service_type_name()->string_view(),
            {service_type_config->service_version_major(),
             static_cast<uint16_t>(service_type_config->service_version_minor())}};
        // TODO: Handle multiple instances. Needs to be converted from integer ID to string.
        // For initial impl, just use service name again.
        socom::Service_instance const inst{service_type_config->service_type_name()->string_view()};

        auto const service_id = service_type_config->service_id();

        std::string shm_path = "/";
        shm_path.append(service_type_config->service_type_name()->string_view())
            .append("_")
            .append(std::to_string(service_id));

        std::string counterpart_shm_path = "/counterpart_";
        counterpart_shm_path.append(service_type_config->service_type_name()->string_view())
            .append("_")
            .append(std::to_string(service_id));

        auto shm_path_result =
            gateway_ipc_binding::fixed_string_from_string<gateway_ipc_binding::Shared_memory_path>(
                shm_path);
        if (!shm_path_result.has_value()) {
            score::mw::log::LogError()
                << "[gatewayd] shm path too long for service_id " << service_id;
            continue;
        }

        auto counterpart_shm_path_result =
            gateway_ipc_binding::fixed_string_from_string<gateway_ipc_binding::Shared_memory_path>(
                counterpart_shm_path);
        if (!counterpart_shm_path_result.has_value()) {
            score::mw::log::LogError()
                << "[gatewayd] counterpart shm path too long for service_id " << service_id;
            continue;
        }

        // TODO: get actual slot size from serializer + 16B SOME/IP header
        if (service_type_config->local_service_instances()) {
            shm_config[iface][inst] = {*shm_path_result, someip::kMaxMessageSize,
                                       someip::kMaxSampleCount};
            // TODO: Needed by the ipc binding for future use of method calls. Set to the smallest
            // possible size for now.
            server_shm_config[iface][inst] = {*counterpart_shm_path_result, 1, 1};
        } else if (service_type_config->remote_service_instances()) {
            server_shm_config[iface][inst] = {*shm_path_result, someip::kMaxMessageSize,
                                              someip::kMaxSampleCount};
            // TODO: Needed by the ipc binding for future use of method calls. Set to the smallest
            // possible size for now.
            shm_config[iface][inst] = {*counterpart_shm_path_result, 1, 1};
        } else {
            score::mw::log::LogError()
                << "[gatewayd] Service " << service_type_config->service_type_name()->string_view()
                << " has no local or remote instances, skipping shared memory config";
        }
    }

    // Create the binding client (auto-sends Connect when the socket is ready).
    auto binding_client = gateway_ipc_binding::Gateway_ipc_binding_client::create(
        *socom_runtime, std::move(ipc_connection),
        gateway_ipc_binding::Shared_memory_manager_factory::create(shm_config),
        {},  // find_service_elements
        score::gateway_ipc_binding::make_shared_memory_configs(server_shm_config), "gatewayd");

    // Wait for the IPC handshake to complete (requires someipd to be running).
    while (!binding_client->is_connected() && !shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (shutdown_requested.load()) {
        score::mw::log::LogInfo()
            << "[gatewayd] Shutdown requested during IPC handshake with someipd, exiting...";
        return 0;
    }
    std::cout << "[gatewayd] IPC connection to someipd established" << std::endl;

    // Create local service instances from configuration
    std::vector<std::unique_ptr<LocalServiceInstance>> local_service_instances;
    for (auto service_type_config : *config->service_types()) {
        auto service_instances = service_type_config->local_service_instances();
        if (service_instances) {
            for (auto const& service_instance_config : *service_instances) {
                std::cout << "Creating local service instance: "
                          << service_type_config->service_type_name()->string_view()
                          << " (service_id=0x" << std::hex << service_type_config->service_id()
                          << std::dec << ", instance_id=0x" << std::hex
                          << service_instance_config->instance_id() << std::dec << ", specifier="
                          << service_instance_config->instance_specifier()->string_view() << ")"
                          << std::endl;

                LocalServiceInstance::CreateAsyncLocalServices(
                    std::shared_ptr<const score::mw_someip_config::ServiceInstance>(
                        config, service_instance_config),
                    std::shared_ptr<const score::mw_someip_config::ServiceType>(
                        config, service_type_config),
                    *socom_runtime, local_service_instances);
            }
        }
    }

    // Create remote service instances from configuration
    std::vector<std::unique_ptr<RemoteServiceInstance>> remote_service_instances;
    for (auto service_type_config : *config->service_types()) {
        auto service_instances = service_type_config->remote_service_instances();
        if (service_instances) {
            for (auto const& service_instance_config : *service_instances) {
                std::cout << "Creating remote service instance: "
                          << service_type_config->service_type_name()->string_view()
                          << " (service_id=0x" << std::hex << service_type_config->service_id()
                          << std::dec << ", instance_id=0x" << std::hex
                          << service_instance_config->instance_id() << std::dec << ", specifier="
                          << service_instance_config->instance_specifier()->string_view() << ")"
                          << std::endl;
                RemoteServiceInstance::CreateAsyncRemoteService(
                    std::shared_ptr<const score::mw_someip_config::ServiceInstance>(
                        config, service_instance_config),
                    std::shared_ptr<const score::mw_someip_config::ServiceType>(
                        config, service_type_config),
                    *socom_runtime, remote_service_instances);
            }
        }
    }

    std::cout << "Gateway started, waiting for shutdown signal..." << std::endl;

    // Main loop - run until shutdown is requested
    while (!shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Shutting down gateway..." << std::endl;

    return 0;
}
