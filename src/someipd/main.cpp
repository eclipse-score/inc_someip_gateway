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
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

#include "local_network_service.h"
#include "remote_network_service.h"
#include "routing.h"
#include "score/filesystem/path.h"
#include "score/gateway_ipc_binding/gateway_ipc_binding_server.hpp"
#include "score/message_passing/server_factory.h"
#include "score/message_passing/service_protocol_config.h"
#include "score/mw/com/runtime.h"
#include "score/mw/log/logging.h"
#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"
#include "src/config/mw_someip_config_generated.h"

using namespace score;
using namespace score::someipd;

// Global flag to control application shutdown
static std::atomic<bool> shutdown_requested{false};

// Signal handler for graceful shutdown
void termination_handler(int /*signal*/) {
    std::cout << "Received termination signal. Initiating graceful shutdown..." << std::endl;
    shutdown_requested.store(true);
}

// Help text, showing usage syntax and available options
void print_help() {
    std::cout << "Syntax: someipd -h/--help\n"
              << "        someipd -c/--configuration <config.bin> "
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
            // No more options
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

    // Both configurations are required, otherwise print help and exit
    if (configuration_path.Empty() || service_instance_manifest_path.Empty()) {
        print_help();
        return EXIT_FAILURE;
    }

    // Read config data
    // TODO: Use memory mapped file instead of copying into buffer
    std::ifstream config_file;
    config_file.open(configuration_path.CStr(), std::ios::binary | std::ios::in);

    if (!config_file.is_open()) {
        score::mw::log::LogFatal() << "Error: Could not open config file " << configuration_path;
        return EXIT_FAILURE;
    }

    config_file.seekg(0, std::ios::end);
    std::streampos length = config_file.tellg();

    if (length <= 0) {
        score::mw::log::LogFatal()
            << "Error: Invalid config file size: " << static_cast<std::size_t>(length);
        config_file.close();
        return EXIT_FAILURE;
    }

    config_file.seekg(0, std::ios::beg);
    auto config_buffer = std::shared_ptr<char>(new char[length]);
    config_file.read(config_buffer.get(), length);
    config_file.close();

    auto config = std::shared_ptr<const score::mw_someip_config::Root>(
        config_buffer, score::mw_someip_config::GetRoot(config_buffer.get()));

    score::mw::com::runtime::InitializeRuntime(
        score::mw::com::runtime::RuntimeConfiguration{service_instance_manifest_path});

    auto socom_runtime = socom::create_runtime();

    // Create the IPC server — socket name and message sizes must match gatewayd's client config
    message_passing::ServiceProtocolConfig const proto{
        "someipd_gatewayd_ipc", someip::kMaxIpcMessageSize, someip::kMaxIpcMessageSize,
        someip::kMaxIpcMessageSize};

    auto ipc_server = message_passing::ServerFactory{}.Create(proto, {10, 1, 10});

    // Create the IPC binding server.
    auto binding_server = gateway_ipc_binding::Gateway_ipc_binding_server::create(
        *socom_runtime, std::move(ipc_server),
        gateway_ipc_binding::Shared_memory_manager_factory::create({}),
        [](gateway_ipc_binding::Client_id, gateway_ipc_binding::Find_service_elements const&,
           bool) {});

    auto start_result = binding_server->start();
    if (!start_result.has_value()) {
        score::mw::log::LogFatal() << "[someipd] Failed to start IPC server";
        return 1;
    }
    std::cout << "[someipd] IPC server started, waiting for gatewayd connection..." << std::endl;

    auto routing = Routing::Create(config);
    if (!routing.has_value()) {
        score::mw::log::LogFatal() << "[someipd] Network stack initialization failed";
        return 1;
    }

    // Create local network services — one client_connector per local service instance,
    // receiving events from gatewayd's server_connectors and forwarding to vsomeip notify().
    std::vector<std::unique_ptr<LocalNetworkService>> local_network_services;
    for (auto service_type_config : *config->service_types()) {
        auto service_instances = service_type_config->local_service_instances();
        if (!service_instances) {
            continue;
        }
        for (auto const& service_instance_config : *service_instances) {
            std::cout << "[someipd] Creating LocalNetworkService: "
                      << service_type_config->service_type_name()->string_view()
                      << " (service_id=0x" << std::hex << service_type_config->service_id()
                      << std::dec << ", instance_id=0x" << std::hex
                      << service_instance_config->instance_id() << std::dec << ")" << std::endl;
            auto create_result = LocalNetworkService::Create(
                std::shared_ptr<const score::mw_someip_config::ServiceInstance>(
                    config, service_instance_config),
                std::shared_ptr<const score::mw_someip_config::ServiceType>(config,
                                                                            service_type_config),
                routing.value().get_application(), *socom_runtime);
            if (!create_result.has_value()) {
                score::mw::log::LogError()
                    << "[someipd] Failed to create LocalNetworkService for "
                    << service_type_config->service_type_name()->string_view();
                continue;
            }
            local_network_services.push_back(std::move(create_result).value());
        }
    }

    // Create remote network services — one server_connector per remote service instance,
    // receiving SOME/IP events via vsomeip and pushing to gatewayd's client_connectors.
    // setup_vsomeip() is deferred until vsomeip reaches ST_REGISTERED (via on_registered below).
    std::vector<std::unique_ptr<RemoteNetworkService>> remote_network_services;
    for (auto service_type_config : *config->service_types()) {
        auto service_instances = service_type_config->remote_service_instances();
        if (!service_instances) {
            continue;
        }
        for (auto const& service_instance_config : *service_instances) {
            std::cout << "[someipd] Creating RemoteNetworkService: "
                      << service_type_config->service_type_name()->string_view()
                      << " (service_id=0x" << std::hex << service_type_config->service_id()
                      << std::dec << ", instance_id=0x" << std::hex
                      << service_instance_config->instance_id() << std::dec << ")" << std::endl;
            auto create_result = RemoteNetworkService::Create(
                std::shared_ptr<const score::mw_someip_config::ServiceInstance>(
                    config, service_instance_config),
                std::shared_ptr<const score::mw_someip_config::ServiceType>(config,
                                                                            service_type_config),
                routing.value().get_application(), *socom_runtime);
            if (!create_result.has_value()) {
                score::mw::log::LogError()
                    << "[someipd] Failed to create RemoteNetworkService for "
                    << service_type_config->service_type_name()->string_view();
                continue;
            }
            remote_network_services.push_back(std::move(create_result).value());
        }
    }

    std::cout << "[someipd] Starting routing loop..." << std::endl;
    routing.value().Run(shutdown_requested, [&remote_network_services]() {
        for (auto& svc : remote_network_services) {
            svc->setup_vsomeip();
        }
    });

    std::cout << "[someipd] Shutting down SOME/IP daemon..." << std::endl;
    return EXIT_SUCCESS;
}
