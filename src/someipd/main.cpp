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

#include "routing.h"
#include "score/filesystem/path.h"
#include "score/mw/com/runtime.h"
#include "score/mw/log/logging.h"
#include "src/common/constants.h"
#include "src/config/mw_someip_config_generated.h"
#include "src/network_service/interfaces/message_transfer.h"

const char* someipd_name = "someipd";

using score::someip_gateway::network_service::interfaces::message_transfer::
    SomeipMessageTransferProxy;
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

    std::vector<score::mw::com::HandleType> handles;

    while (handles.empty() && !shutdown_requested.load()) {
        auto find_result = SomeipMessageTransferProxy::FindService(
            score::mw::com::InstanceSpecifier::Create(std::string("someipd/gatewayd_messages"))
                .value());

        if (!find_result.has_value()) {
            std::cerr << "[someipd] Error finding service: " << find_result.error().Message()
                      << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        handles = find_result.value();

        if (handles.empty()) {
            std::cout << "[someipd] Waiting for gatewayd to start..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (shutdown_requested.load()) {
        return EXIT_SUCCESS;
    }

    // Proxy for receiving messages from gatewayd to be sent via SOME/IP
    auto proxy = SomeipMessageTransferProxy::Create(handles.front()).value();
    proxy.message_.Subscribe(score::someip::max_sample_count);

    // Skeleton for transmitting messages from the network to gatewayd
    // TODO: Error handling for instance specifier creation
    auto create_result = SomeipMessageTransferSkeleton::Create(
        score::mw::com::InstanceSpecifier::Create(std::string("someipd/someipd_messages")).value());

    auto skeleton = std::move(create_result).value();

    // TODO: Error handling
    (void)skeleton.OfferService();

    auto routing = score::someipd::Routing::Create(config, std::move(proxy), std::move(skeleton));
    if (!routing.has_value()) {
        score::mw::log::LogFatal() << "[someipd] Network stack initialization failed";
        return 1;
    }

    std::cout << "[someipd] Starting routing loop..." << std::endl;
    routing.value().Run(shutdown_requested);

    std::cout << "[someipd] Shutting down SOME/IP daemon..." << std::endl;
    return EXIT_SUCCESS;
}
