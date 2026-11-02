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

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "impl/gatewayd_services.h"
#include "impl/routing.h"
#include "score/bridged_services/bridged_service_configs.h"
#include "score/filesystem/path.h"
#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/mw/com/runtime.h"
#include "score/mw/log/logging.h"
#include "score/socom/runtime.hpp"

using namespace score;
using namespace score::someipd;
using score::bridged_services::kSomeipd_service_specifier;

// Help text, showing usage syntax and available options
static void print_help() {
    std::cout << "Syntax: someipd -h/--help\n"
              << "        someipd -s/--service_instance_manifest <manifest.json>\n"
              << "\n";

    std::cout << "Options:\n"
              << " -h/--help Displays this help\n"
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

    const char* const short_opts = "hs:";
    const option long_opts[] = {{"help", no_argument, nullptr, 'h'},
                                {"service_instance_manifest", required_argument, nullptr, 's'},
                                {nullptr, no_argument, nullptr, 0}};

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

    if (service_instance_manifest_path.Empty()) {
        print_help();
        return EXIT_FAILURE;
    }

    score::mw::com::runtime::InitializeRuntime(
        score::mw::com::runtime::RuntimeConfiguration{service_instance_manifest_path});

    auto socom_runtime = socom::create_runtime();

    auto routing = Routing::Create();
    if (!routing.has_value()) {
        score::mw::log::LogFatal() << "[someipd] Network stack initialization failed";
        return 1;
    }

    GatewaydServices gatewayd_services{*routing.value(), *socom_runtime};

    // Declared last, so it is destroyed first. Its handler uses gatewayd_services.
    auto binding_server = gateway_ipc_binding::mw_com::create_server(
        *socom_runtime, kSomeipd_service_specifier,
        [&gatewayd_services](
            score::cpp::span<std::byte const> gateway_config,
            gateway_ipc_binding::mw_com::Add_bridged_services const& add_bridged_services) {
            return gatewayd_services.Add(gateway_config, add_bridged_services);
        });
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

    bool clean_exit = true;

    score::mw::log::LogInfo() << "[someipd] Starting routing loop...";
    routing.value()->Run([&shutdown_signals, &clean_exit]() {
        int signal_number = 0;
        if (sigwait(&shutdown_signals, &signal_number) != 0) {
            score::mw::log::LogError() << "[someipd] sigwait failed, shutting down";
            clean_exit = false;
            return;
        }
        score::mw::log::LogWarn() << "[someipd] Received signal " << signal_number
                                  << ", shutting down...";
    });

    score::mw::log::LogInfo() << "[someipd] Shutting down SOME/IP daemon...";
    return clean_exit ? EXIT_SUCCESS : EXIT_FAILURE;
}
