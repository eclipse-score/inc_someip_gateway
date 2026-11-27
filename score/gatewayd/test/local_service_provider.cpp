/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
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

// Test provider of a local service: offers one mw::com instance with one event until SIGTERM, so
// that gatewayd finds the service and someipd offers it on SOME/IP.
//
// Usage: local_service_provider <mw_com_config.json> <instance_specifier> <event_name>

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include "score/filesystem/path.h"
#include "score/mw/com/runtime.h"
#include "score/mw/com/runtime_configuration.h"
#include "score/mw/com/types.h"
#include "score/serializer/pre_serialized_data.h"

namespace {

std::atomic<bool> g_stop{false};

void OnSignal(int /*signal*/) { g_stop.store(true); }

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: local_service_provider <mw_com_config.json> <instance_specifier> "
                     "<event_name>"
                  << std::endl;
        return 1;
    }
    std::signal(SIGTERM, OnSignal);
    std::signal(SIGINT, OnSignal);

    score::mw::com::runtime::InitializeRuntime(
        score::mw::com::runtime::RuntimeConfiguration{score::filesystem::Path{argv[1]}});

    auto const specifier = score::mw::com::InstanceSpecifier::Create(std::string{argv[2]});
    if (!specifier.has_value()) {
        std::cerr << "[provider] Invalid instance specifier " << argv[2] << std::endl;
        return 1;
    }

    // argv outlives the skeleton, so the event name may point into it.
    std::array<score::mw::com::EventInfo, 1> const events{{
        {std::string_view{argv[3]},
         score::mw::com::DataTypeMetaInfo{
             score::someip_gateway::serializer::get_size_of_pre_serialized_data(8U),
             alignof(score::someip_gateway::serializer::PreSerializedData<0>)}},
    }};
    score::mw::com::GenericSkeletonServiceElementInfo create_params;
    create_params.events = events;

    auto skeleton = score::mw::com::GenericSkeleton::Create(specifier.value(), create_params);
    if (!skeleton.has_value()) {
        std::cerr << "[provider] GenericSkeleton::Create failed for " << argv[2] << std::endl;
        return 1;
    }
    if (!skeleton.value().OfferService().has_value()) {
        std::cerr << "[provider] OfferService failed for " << argv[2] << std::endl;
        return 1;
    }
    std::cout << "[provider] offered " << argv[2] << std::endl;

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    skeleton.value().StopOfferService();
    std::cout << "[provider] stopped offering " << argv[2] << std::endl;
    return 0;
}
