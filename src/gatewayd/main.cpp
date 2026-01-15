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

#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

#include "local_service_instance.h"
#include "remote_service_instance.h"
#include "score/mw/com/runtime.h"
#include "score/mw/com/types.h"
#include "src/gatewayd/gatewayd_config_generated.h"
#include "src/network_service/interfaces/message_transfer.h"

// In the main file we are not in any namespace
using namespace score::someip_gateway::gatewayd;
using score::mw::com::InstanceSpecifier;
using score::someip_gateway::network_service::interfaces::message_transfer::
    SomeipMessageTransferSkeleton;

// Global flag to control application shutdown
static std::atomic<bool> shutdown_requested{false};

// Signal handler for graceful shutdown
void termination_handler(int /*signal*/) {
    std::cout << "Received termination signal. Initiating graceful shutdown..." << std::endl;
    shutdown_requested.store(true);
}

int main(int argc, const char* argv[]) {
    // Register signal handlers for graceful shutdown
    std::signal(SIGTERM, termination_handler);
    std::signal(SIGINT, termination_handler);

    // Read config data
    // TODO: Be more flexible with the path
    // TODO: Use memory mapped file instead of copying into buffer
    std::ifstream config_file;
    config_file.open("src/gatewayd/etc/gatewayd_config.bin", std::ios::binary | std::ios::in);

    if (!config_file.is_open()) {
        std::cerr << "Error: Could not open config file 'src/gatewayd/etc/gatewayd_config.bin'"
                  << std::endl;
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

    auto config =
        std::shared_ptr<const config::Root>(config_buffer, config::GetRoot(config_buffer.get()));

    score::mw::com::runtime::InitializeRuntime(argc, argv);

    // Create service instances from configuration
    if (config->local_service_instances() == nullptr) {
        std::cerr << "No local service instances configured" << std::endl;
        return 1;
    }

    std::vector<std::unique_ptr<LocalServiceInstance>> local_service_instances;

    for (auto service_instance_config : *config->local_service_instances()) {
        // Create SomeipMessageTransferSkeletons for each event/method of the service instance
        std::map<uint16_t, SomeipMessageTransferSkeleton> skeletons_map;

        // Retrieve IDs for IPC InstanceSpecifier creation
        auto service_id = std::to_string(service_instance_config->someip_service_id());
        auto instance_id = std::to_string(service_instance_config->someip_service_instance_id());
        for (auto event : *service_instance_config->events()) {
            auto method_id = std::to_string(event->someip_method_id());

            // Instance specifier format: "SomeipMessage_<service_id>_<instance_id>_<method_id>"
            auto instance_specifier_result = InstanceSpecifier::Create(
                "SomeipMessage_" + service_id + "_" + instance_id + "_" + method_id);
            if (!instance_specifier_result.has_value()) {
                std::cerr << "Error: Failed to create InstanceSpecifier within local service "
                             "instance. SID= "
                          << service_id << ", IID= " << instance_id << ", MID= " << method_id
                          << ", Error= " << instance_specifier_result.error().Message()
                          << std::endl;
                return 1;
            }

            auto skeleton_create_result =
                SomeipMessageTransferSkeleton::Create(instance_specifier_result.value());
            if (!skeleton_create_result.has_value()) {
                std::cerr << "Error: Failed to create SomeipMessageTransferSkeleton for local "
                             "service instance: SID= "
                          << service_id << ", IID= " << instance_id << ", MID= " << method_id
                          << ", Error= " << skeleton_create_result.error().Message() << std::endl;
                return 1;
            }

            auto someip_message_skeleton = std::move(skeleton_create_result).value();
            // TODO: Error handling
            (void)someip_message_skeleton.OfferService();

            skeletons_map.try_emplace(event->someip_method_id(),
                                      std::move(someip_message_skeleton));
        }
        LocalServiceInstance::CreateAsyncLocalService(
            std::shared_ptr<const config::ServiceInstance>(config, service_instance_config),
            std::move(skeletons_map), local_service_instances);
    }

    // Create service instances from configuration
    if (config->remote_service_instances() == nullptr) {
        std::cerr << "No remote service instances configured" << std::endl;
        return 1;
    }

    std::vector<std::unique_ptr<RemoteServiceInstance>> remote_service_instances;
    for (auto service_instance_config : *config->remote_service_instances()) {
        RemoteServiceInstance::CreateAsyncRemoteService(
            std::shared_ptr<const config::ServiceInstance>(config, service_instance_config),
            remote_service_instances);
    }

    std::cout << "Gateway started, waiting for shutdown signal..." << std::endl;

    // Main loop - run until shutdown is requested
    while (!shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Shutting down gateway..." << std::endl;

    return 0;
}
