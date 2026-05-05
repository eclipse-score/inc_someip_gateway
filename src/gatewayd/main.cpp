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
#include <thread>

#include "local_service_instance.h"
#include "remote_service_instance.h"
#include "score/filesystem/path.h"
#include "score/mw/com/runtime.h"
#include "score/mw/com/types.h"
#include "src/config/mw_someip_config_generated.h"
#include "src/network_service/interfaces/message_transfer.h"

// In the main file we are not in any namespace
using namespace score::someip_gateway::gatewayd;
using score::someip_gateway::network_service::interfaces::message_transfer::
    SomeipMessageTransferSkeleton;

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
        std::cerr << "Error: Could not open config file " << configuration_path.CStr() << std::endl;
        return 1;
    }

    config_file.seekg(0, std::ios::end);
    std::streampos length = config_file.tellg();

    if (length <= 0) {
        std::cerr << "Error: Invalid config file size: " << length << std::endl;
        config_file.close();
        return 1;
    }

    config_file.seekg(0, std::ios::beg);
    auto config_buffer = std::shared_ptr<char>(new char[length]);
    config_file.read(config_buffer.get(), length);
    config_file.close();

    auto config = std::shared_ptr<const score::mw_someip_config::Root>(
        config_buffer, score::mw_someip_config::GetRoot(config_buffer.get()));

    score::mw::com::runtime::InitializeRuntime(
        score::mw::com::runtime::RuntimeConfiguration{service_instance_manifest_path});

    // TODO: Need to come up with a proper scheme how to generate instance specifiers
    auto create_result = SomeipMessageTransferSkeleton::Create(
        score::mw::com::InstanceSpecifier::Create(std::string("gatewayd/gatewayd_messages"))
            .value());
    // TODO: Error handling
    auto someip_message_skeleton = std::move(create_result).value();

    // TODO: Error handling
    (void)someip_message_skeleton.OfferService();

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
                    someip_message_skeleton, local_service_instances);
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
                    remote_service_instances);
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
