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
#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "impl/local_service_instance.h"
#include "impl/remote_service_instance.h"
#include "score/config/mw_someip_config_generated.h"
#include "score/filesystem/path.h"
#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/mw/com/runtime.h"
#include "score/mw/log/logging.h"
#include "score/serializer/serializer.h"
#include "score/socom/final_action.hpp"
#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"

// In the main file we are not in any namespace
using namespace score;
using namespace score::someip_gateway::gatewayd;

// Global flag to control application shutdown
static std::atomic<bool> shutdown_requested{false};

// mw::com instance specifier of the SomeipdService that someipd offers; someipd uses the same.
static constexpr const char* kSomeipdServiceSpecifier = "someipd/daemon";

/// SOCom interface of a service on the mw::com link to someipd: bridged_<service_type_name>.
/// Allocates, so it is only called at startup.
static socom::Service_interface_identifier bridged_interface(
    const score::mw_someip_config::ServiceType& service_type_config) {
    return socom::Service_interface_identifier{
        std::string{"bridged_"}.append(service_type_config.service_type_name()->string_view()),
        {service_type_config.service_version_major(),
         static_cast<uint16_t>(service_type_config.service_version_minor())}};
}

/// Calculates the required shared memory slot size for one event
/// (Largest serialized event payload + SOME/IP header).
///
/// Since encoding affects the payload size, we query the serializer plugin
/// directly. If the event size is unknown or the serializer is missing, we safely
/// fall back to the max transport limit.
static std::size_t event_slot_size(std::string_view const service_type_name,
                                   std::string_view const event_name) {
    const score_com_serializer* serializer = nullptr;
    if (score_com_serializer_get(service_type_name.data(), service_type_name.size(),
                                 score_com_serializer_element_type_event, event_name.data(),
                                 event_name.size(),
                                 &serializer) != score_com_serializer_result_ok) {
        score::mw::log::LogWarn() << "[gatewayd] No serializer for " << service_type_name
                                  << "::" << event_name << ", using maximum slot size";
        return someip::kMaxMessageSize;
    }

    auto const max_serialized_size = score_com_serializer_get_max_serialized_size(serializer);
    if (max_serialized_size == 0) {
        score::mw::log::LogWarn() << "[gatewayd] Serializer reports no maximum size for "
                                  << service_type_name << "::" << event_name
                                  << ", using maximum slot size";
        return someip::kMaxMessageSize;
    }

    return max_serialized_size + someip::kSomeipFullHeaderSize;
}

/// Bridged service instances on the mw::com link to someipd, one per service type of the gateway
/// configuration: gatewayd provides what local applications offer and consumes what the network
/// offers. Every event sample holds the event's largest SOME/IP message, header included. someipd
/// gets these sizes from the binding. Called once at startup, after the serializer is initialized.
static gateway_ipc_binding::mw_com::Service_configs bridged_service_configs(
    const score::mw_someip_config::Root& config) {
    gateway_ipc_binding::mw_com::Service_configs services;
    for (const auto* service_type_config : *config.service_types()) {
        gateway_ipc_binding::mw_com::Role role{};
        if (service_type_config->local_service_instances() != nullptr &&
            !service_type_config->local_service_instances()->empty()) {
            role = gateway_ipc_binding::mw_com::Role::provider;
        } else if (service_type_config->remote_service_instances() != nullptr &&
                   !service_type_config->remote_service_instances()->empty()) {
            role = gateway_ipc_binding::mw_com::Role::consumer;
        } else {
            continue;
        }
        // The binding needs at least one event per service.
        if (service_type_config->events() == nullptr || service_type_config->events()->empty()) {
            continue;
        }

        std::vector<gateway_ipc_binding::mw_com::Event_config> events;
        auto const service_type_name = service_type_config->service_type_name()->string_view();
        for (const auto* event_config : *service_type_config->events()) {
            events.push_back({.name = event_config->event_name()->str(),
                              .max_payload_size = event_slot_size(
                                  service_type_name, event_config->event_name()->string_view())});
        }

        services.push_back(
            {.interface = bridged_interface(*service_type_config),
             .instance = socom::Service_instance{service_type_name},
             .instance_specifier = std::string{"bridged_ipc/"}.append(service_type_name),
             .role = role,
             .events = std::move(events),
             .max_sample_count = someip::kMaxSampleCount});
    }
    return services;
}

// Signal handler for graceful shutdown
static void termination_handler(int /*signal*/) {
    std::cout << "Received termination signal. Initiating graceful shutdown..." << std::endl;
    shutdown_requested.store(true);
}

// Help text, showing usage syntax and available options
static void print_help() {
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

    // Bridged services on the link to someipd, set up eagerly. Exits if any of them cannot be.
    auto binding_client = gateway_ipc_binding::mw_com::create_client(
        *socom_runtime, kSomeipdServiceSpecifier, bridged_service_configs(*config), "gatewayd");
    if (binding_client == nullptr) {
        score::mw::log::LogFatal()
            << "[gatewayd] Failed to create the mw::com IPC binding to someipd";
        return 1;
    }

    // Wait until someipd offers SomeipdService (requires someipd to be running).
    while (!binding_client->is_connected() && !shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (shutdown_requested.load()) {
        score::mw::log::LogInfo()
            << "[gatewayd] Shutdown requested while waiting for someipd, exiting...";
        return 0;
    }
    std::cout << "[gatewayd] Connected to someipd over the mw::com IPC binding" << std::endl;

    // Create local service instances from configuration
    std::vector<std::unique_ptr<LocalServiceInstance>> local_service_instances;
    for (const auto* service_type_config : *config->service_types()) {
        const auto* service_instances = service_type_config->local_service_instances();
        if (service_instances != nullptr) {
            for (auto const& service_instance_config : *service_instances) {
                std::cout << "Creating local service instance: "
                          << service_type_config->service_type_name()->string_view()
                          << " (service_id=0x" << std::hex << service_type_config->service_id()
                          << std::dec << ", instance_id=0x" << std::hex
                          << service_instance_config->instance_id() << std::dec << ", specifier="
                          << service_instance_config->instance_specifier()->string_view() << ")"
                          << std::endl;

                if (const auto create_local_result = LocalServiceInstance::CreateAsyncLocalServices(
                        std::shared_ptr<const score::mw_someip_config::ServiceInstance>(
                            config, service_instance_config),
                        std::shared_ptr<const score::mw_someip_config::ServiceType>(
                            config, service_type_config),
                        bridged_interface(*service_type_config), *socom_runtime,
                        local_service_instances);
                    !create_local_result) {
                    score::mw::log::LogError()
                        << "[gatewayd] Failed to start local service discovery for "
                        << service_type_config->service_type_name()->string_view() << " (specifier="
                        << service_instance_config->instance_specifier()->string_view()
                        << "): " << create_local_result.error().Message();
                }
            }
        }
    }

    // Create remote service instances from configuration
    std::vector<std::unique_ptr<RemoteServiceInstance>> remote_service_instances;
    for (const auto* service_type_config : *config->service_types()) {
        const auto* service_instances = service_type_config->remote_service_instances();
        if (service_instances != nullptr) {
            for (auto const& service_instance_config : *service_instances) {
                std::cout << "Creating remote service instance: "
                          << service_type_config->service_type_name()->string_view()
                          << " (service_id=0x" << std::hex << service_type_config->service_id()
                          << std::dec << ", instance_id=0x" << std::hex
                          << service_instance_config->instance_id() << std::dec << ", specifier="
                          << service_instance_config->instance_specifier()->string_view() << ")"
                          << std::endl;
                if (const auto create_remote_result =
                        RemoteServiceInstance::CreateAsyncRemoteService(
                            std::shared_ptr<const score::mw_someip_config::ServiceInstance>(
                                config, service_instance_config),
                            std::shared_ptr<const score::mw_someip_config::ServiceType>(
                                config, service_type_config),
                            bridged_interface(*service_type_config), *socom_runtime,
                            remote_service_instances);
                    !create_remote_result) {
                    score::mw::log::LogError()
                        << "[gatewayd] Failed to start remote service discovery for "
                        << service_type_config->service_type_name()->string_view() << " (specifier="
                        << service_instance_config->instance_specifier()->string_view()
                        << "): " << create_remote_result.error().Message();
                }
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
