/********************************************************************************
 * Copyright (c) 2025 Elektrobit Automotive GmbH
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

#include "gateway.h"

#include "dlopen.hpp"

#include <score/gateway/payload_transformation_plugin_interface.hpp>
#include <score/gateway/plugin_handle.hpp>
#include <score/socom/runtime.hpp>

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

using namespace std::chrono_literals;

using score::gateway::Someip_network_plugin_factory;
using score::gateway::Someip_network_plugin_interface;
using score::socom::create_runtime;
using score::socom::Event_update_callback;
using score::socom::Runtime;
using score::socom::Service_instance;

namespace score::gateway {

Gateway::Gateway(socom::Runtime::Uptr runtime, Dlopen::Uptr dlopen,
                 Someip_network_plugin_interface::Uptr plugin)
    : runtime_(std::move(runtime)), dlopen_{std::move(dlopen)}, network_plugin_(std::move(plugin))
{
}

Gateway::Create_result Gateway::create(std::string const& plugin_path,
                                       std::string_view const& network_interface,
                                       std::string_view const& ip_address,
                                       std::vector<std::string> const& manifests)
{
    auto dlopen_result = create_dlopen(plugin_path);
    if (!dlopen_result.has_value()) {
        return Create_result::unexpected_type("Unable to load plugin: " + dlopen_result.error());
    }

    Runtime::Uptr runtime = create_runtime();
    assert(runtime != nullptr);

    assert(1 <= Plugin_handle<Someip_network_plugin_factory>::get_num_plugins());

    auto plugin =
        Plugin_handle<Someip_network_plugin_factory>::get_plugin_functions().begin()->second(
            *runtime, network_interface, ip_address, manifests);

    if (nullptr == plugin) {
        return Create_result::unexpected_type("Unable to create plugin instance");
    }

    return Gateway{std::move(runtime), std::move(*dlopen_result), std::move(plugin)};
}

int Gateway::run(const std::chrono::milliseconds cycle_time, const std::size_t num_cycles)
{
    auto const subscription = runtime_->subscribe_find_service(
        [](auto const& interface, auto const& instance, auto status) {
            std::cout << "Find service update: Interface " << interface.id << " v"
                      << interface.version.major << "." << interface.version.minor << ", Instance "
                      << instance << ", Status "
                      << (status == socom::Find_result_status::added ? "Added" : "Deleted")
                      << std::endl;
        },
        std::nullopt, std::nullopt, std::nullopt);

    std::vector<score::gateway::Payload_transformation_plugin_interface::Uptr> plugin_instances;
    plugin_instances.reserve(
        Plugin_handle<Payload_transformation_plugin_factory>::get_num_plugins());
    for (auto const& [key, factory] :
         Plugin_handle<Payload_transformation_plugin_factory>::get_plugin_functions()) {
        auto plugin = factory(*runtime_);
        if (nullptr == plugin) {
            std::cerr << "Unable to create payload transformation plugin, terminating."
                      << std::endl;
            return EXIT_FAILURE;
        }
        plugin_instances.push_back(std::move(plugin));
        std::cout << "Successfully loaded payload transformation plugin" << std::endl;
    }

    if (plugin_instances.empty()) {
        std::cerr << "No payload transformation plugin loaded, terminating." << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "Starting to send data\n";

    std::size_t cycle = 0U;

    for (cycle = 0U; cycle < num_cycles || num_cycles == 0U; ++cycle) {
        network_plugin_->poll();
        std::this_thread::sleep_for(cycle_time);
    }

    return EXIT_SUCCESS;
}

} // namespace score::gateway
