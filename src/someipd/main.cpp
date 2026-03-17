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
#include <cstdlib>
#include <memory>

#include "score/mw/log/logging.h"
#include "src/someipd/gateway_routing.h"
#include "src/someipd/mwcom_adapter.h"
#include "src/someipd/someipd_config.h"
#include "src/someipd/vsomeip_adapter.h"

using score::someip_gateway::someipd::GatewayRouting;
using score::someip_gateway::someipd::MwcomAdapter;
using score::someip_gateway::someipd::SomeipDConfig;
using score::someip_gateway::someipd::VsomeipAdapter;

static std::atomic<bool> shutdown_requested{false};

void termination_handler(int /*signal*/) { shutdown_requested.store(true); }

std::optional<std::string_view> ParseConfigPath(int argc, const char* argv[]) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string_view{argv[i]} == "-someipd_config") {
            return argv[i + 1];
        }
    }
    return std::nullopt;
}

void LogConfig(const SomeipDConfig& config) {
    using score::mw::log::LogHex16;
    score::mw::log::LogInfo() << "Loaded someipd config";
    for (const auto& svc : config.offered_services) {
        score::mw::log::LogInfo() << "Offered service: " << LogHex16{svc.service_id} << ":"
                                  << LogHex16{svc.instance_id} << " on port "
                                  << svc.unreliable_port;
        for (const auto& ev : svc.events) {
            score::mw::log::LogInfo() << "  Event: " << LogHex16{ev.event_id} << " in event group "
                                      << LogHex16{ev.eventgroup_id};
        }
    }
    for (const auto& svc : config.subscribed_services) {
        score::mw::log::LogInfo() << "Subscribed service: " << LogHex16{svc.service_id} << ":"
                                  << LogHex16{svc.instance_id};
        for (const auto& ev : svc.events) {
            score::mw::log::LogInfo() << "  Event: " << LogHex16{ev.event_id} << " in event group "
                                      << LogHex16{ev.eventgroup_id};
        }
    }
}

int main(int argc, const char* argv[]) {
    score::mw::log::LogInfo() << "Starting SOME/IP daemon...";
    std::signal(SIGTERM, termination_handler);
    std::signal(SIGINT, termination_handler);

    auto someipd_config_path = ParseConfigPath(argc, argv);
    if (!someipd_config_path.has_value()) {
        score::mw::log::LogError() << "Mandatory argument '-someipd_config' is missing.";
        return EXIT_FAILURE;
    }

    SomeipDConfig config{};
    config = score::someip_gateway::someipd::ReadSomeipDConfig(std::string(someipd_config_path.value()));
    LogConfig(config);

    // Create adapters — swap these to use different SOME/IP stacks or IPC frameworks.
    auto network_stack = std::make_unique<VsomeipAdapter>("someipd");
    if (!network_stack->Init()) {
        score::mw::log::LogError() << "Network stack initialization failed";
        return EXIT_FAILURE;
    }

    auto internal_ipc =
        std::make_unique<MwcomAdapter>("someipd/gatewayd_messages", "someipd/someipd_messages", 10);
    internal_ipc->Init(argc, argv);

    GatewayRouting routing(std::move(network_stack), std::move(internal_ipc), std::move(config));
    routing.Run(shutdown_requested);

    return EXIT_SUCCESS;
}
