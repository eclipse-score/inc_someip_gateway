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
#include <pthread.h>
#include <signal.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "impl/local_network_service.h"
#include "impl/remote_network_service.h"
#include "impl/routing.h"
#include "score/config/mw_someip_config_generated.h"
#include "score/filesystem/path.h"
#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/mw/com/runtime.h"
#include "score/mw/log/logging.h"
#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"

using namespace score;
using namespace score::someipd;

// mw::com instance specifier of the SomeipdService that someipd offers; gatewayd uses the same.
static constexpr const char* kSomeipdServiceSpecifier = "someipd/daemon";

/// SOCom interface of a service on the mw::com link to gatewayd: bridged_<service_type_name>.
/// Allocates, so it is only called at startup.
static socom::Service_interface_identifier bridged_interface(
    const score::mw_someip_config::ServiceType& service_type_config) {
    return socom::Service_interface_identifier{
        std::string{"bridged_"}.append(service_type_config.service_type_name()->string_view()),
        {service_type_config.service_version_major(),
         static_cast<uint16_t>(service_type_config.service_version_minor())}};
}

/// Bridged service instances on the mw::com link to gatewayd, one per service type of the gateway
/// configuration, mirroring gatewayd: someipd consumes what local applications offer and provides
/// what the network offers. Every event sample holds the largest SOME/IP message, header
/// included. Called once at startup.
static gateway_ipc_binding::mw_com::Service_configs bridged_service_configs(
    const score::mw_someip_config::Root& config) {
    gateway_ipc_binding::mw_com::Service_configs services;
    for (const auto* service_type_config : *config.service_types()) {
        gateway_ipc_binding::mw_com::Role role{};
        if (service_type_config->local_service_instances() != nullptr &&
            !service_type_config->local_service_instances()->empty()) {
            role = gateway_ipc_binding::mw_com::Role::consumer;
        } else if (service_type_config->remote_service_instances() != nullptr &&
                   !service_type_config->remote_service_instances()->empty()) {
            role = gateway_ipc_binding::mw_com::Role::provider;
        } else {
            continue;
        }
        // The binding needs at least one event per service.
        if (service_type_config->events() == nullptr || service_type_config->events()->empty()) {
            continue;
        }

        std::vector<gateway_ipc_binding::mw_com::Event_config> events;
        for (const auto* event_config : *service_type_config->events()) {
            events.push_back({.name = event_config->event_name()->str(),
                              .max_payload_size = someip::kMaxMessageSize});
        }

        auto const service_type_name = service_type_config->service_type_name()->string_view();
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

// Help text, showing usage syntax and available options
static void print_help() {
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
    // Block before any thread starts so every thread inherits the mask.
    // sigwait below is the only receiver.
    sigset_t shutdown_signals;
    sigemptyset(&shutdown_signals);
    sigaddset(&shutdown_signals, SIGTERM);
    sigaddset(&shutdown_signals, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &shutdown_signals, nullptr) != 0) {
        score::mw::log::LogFatal() << "[someipd] Failed to block SIGTERM and SIGINT";
        return EXIT_FAILURE;
    }

    const char* const short_opts = "hc:s:";
    const option long_opts[] = {{"help", no_argument, nullptr, 'h'},
                                {"configuration", required_argument, nullptr, 'c'},
                                {"service_instance_manifest", required_argument, nullptr, 's'},
                                {nullptr, no_argument, nullptr, 0}};

    score::filesystem::Path configuration_path{};
    score::filesystem::Path service_instance_manifest_path{};

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

    // Both configuration files are required, otherwise print help and exit
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
    auto config_buffer = std::shared_ptr<char[]>(new char[length]);
    config_file.read(config_buffer.get(), length);
    config_file.close();

    auto config = std::shared_ptr<const score::mw_someip_config::Root>(
        config_buffer, score::mw_someip_config::GetRoot(config_buffer.get()));

    score::mw::com::runtime::InitializeRuntime(
        score::mw::com::runtime::RuntimeConfiguration{service_instance_manifest_path});

    auto socom_runtime = socom::create_runtime();

    // Offers SomeipdService, then sets up the bridged services on the link to gatewayd.
    auto binding_server = gateway_ipc_binding::mw_com::create_server(
        *socom_runtime, kSomeipdServiceSpecifier, bridged_service_configs(*config));
    if (binding_server == nullptr) {
        score::mw::log::LogFatal() << "[someipd] Failed to create the mw::com IPC binding";
        return 1;
    }

    auto start_result = binding_server->start();
    if (!start_result.has_value()) {
        score::mw::log::LogFatal()
            << "[someipd] Failed to start the mw::com IPC binding: " << start_result.error();
        return 1;
    }
    std::cout << "[someipd] mw::com IPC binding started, SomeipdService offered to gatewayd"
              << std::endl;

    auto routing = Routing::Create(config);
    if (!routing.has_value()) {
        score::mw::log::LogFatal() << "[someipd] Network stack initialization failed";
        return 1;
    }

    // Create local network services — one client_connector per local service instance,
    // receiving events from gatewayd's server_connectors and forwarding to vsomeip notify().
    std::vector<std::unique_ptr<LocalNetworkService>> local_network_services;
    for (const auto* service_type_config : *config->service_types()) {
        const auto* service_instances = service_type_config->local_service_instances();
        if (service_instances == nullptr) {
            continue;
        }
        for (auto const& service_instance_config : *service_instances) {
            score::mw::log::LogInfo()
                << "[someipd] Creating LocalNetworkService: "
                << service_type_config->service_type_name()->string_view() << " service_id=0x"
                << score::mw::log::LogHex16{service_type_config->service_id()} << " instance_id=0x"
                << score::mw::log::LogHex16{service_instance_config->instance_id()};
            auto create_result = LocalNetworkService::Create(
                std::shared_ptr<const score::mw_someip_config::ServiceInstance>(
                    config, service_instance_config),
                std::shared_ptr<const score::mw_someip_config::ServiceType>(config,
                                                                            service_type_config),
                routing.value().get_application(), *socom_runtime,
                bridged_interface(*service_type_config));
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
    for (const auto* service_type_config : *config->service_types()) {
        const auto* service_instances = service_type_config->remote_service_instances();
        if (service_instances == nullptr) {
            continue;
        }
        for (auto const& service_instance_config : *service_instances) {
            score::mw::log::LogInfo()
                << "[someipd] Creating RemoteNetworkService: "
                << service_type_config->service_type_name()->string_view() << " service_id=0x"
                << score::mw::log::LogHex16{service_type_config->service_id()} << " instance_id=0x"
                << score::mw::log::LogHex16{service_instance_config->instance_id()};
            auto create_result = RemoteNetworkService::Create(
                std::shared_ptr<const score::mw_someip_config::ServiceInstance>(
                    config, service_instance_config),
                std::shared_ptr<const score::mw_someip_config::ServiceType>(config,
                                                                            service_type_config),
                routing.value().get_application(), *socom_runtime,
                bridged_interface(*service_type_config));
            if (!create_result.has_value()) {
                score::mw::log::LogError()
                    << "[someipd] Failed to create RemoteNetworkService for "
                    << service_type_config->service_type_name()->string_view();
                continue;
            }
            remote_network_services.push_back(std::move(create_result).value());
        }
    }

    bool clean_exit = true;

    score::mw::log::LogInfo() << "[someipd] Starting routing loop...";
    routing.value().Run(
        [&shutdown_signals, &clean_exit]() {
            int signal_number = 0;
            if (sigwait(&shutdown_signals, &signal_number) != 0) {
                score::mw::log::LogError() << "[someipd] sigwait failed, shutting down";
                clean_exit = false;
                return;
            }
            score::mw::log::LogWarn()
                << "[someipd] Received signal " << signal_number << ", shutting down...";
        },
        [&remote_network_services]() {
            for (auto& svc : remote_network_services) {
                svc->setup_vsomeip();
            }
        });

    score::mw::log::LogInfo() << "[someipd] Shutting down SOME/IP daemon...";
    return clean_exit ? EXIT_SUCCESS : EXIT_FAILURE;
}
