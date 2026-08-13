// *******************************************************************************
// Copyright (c) 2026 Contributors to the Eclipse Foundation
//
// See the NOTICE file(s) distributed with this work for additional
// information regarding copyright ownership.
//
// This program and the accompanying materials are made available under the
// terms of the Apache License Version 2.0 which is available at
// https://www.apache.org/licenses/LICENSE-2.0
//
// SPDX-License-Identifier: Apache-2.0
// *******************************************************************************

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <string>
#include <thread>

#include "score/filesystem/path.h"
#include "score/mw/com/runtime.h"
#include "score/mw/com/runtime_configuration.h"
#include "score/mw/com/types.h"
#include "score/mw/log/logging.h"
#include "score/serializer/pre_serialized_data.h"
#include "tests/tc8_conformance/application/shared/tc8_ets_service.h"

namespace {
using Payload = score::someip_gateway::serializer::PreSerializedData<tc8_ets_service::kMaxPayloadBytes>;
}  // namespace

constexpr std::string_view kInstanceSpecifier{"tc8/tc8_service"};

static std::atomic<bool> g_shutdown_requested{false};

static void SignalHandler(int /*signal*/) {
    g_shutdown_requested.store(true);
}

int main(int argc, char* argv[]) {
    std::signal(SIGTERM, SignalHandler);
    std::signal(SIGINT, SignalHandler);

    std::string manifest_path;
    for (int i = 1; i < argc; ++i) {
        if ((std::strcmp(argv[i], "-s") == 0) && (i + 1 < argc)) {
            manifest_path = argv[++i];
        }
    }
    if (manifest_path.empty()) {
        score::mw::log::LogError() << "[tc8_ets_stub] Usage: tc8_ets_stub -s <mw_com_config.json>";
        return 1;
    }

    score::mw::com::runtime::InitializeRuntime(score::mw::com::runtime::RuntimeConfiguration{
        score::filesystem::Path{manifest_path}});

    const auto specifier_result = score::mw::com::InstanceSpecifier::Create(std::string{kInstanceSpecifier});
    if (!specifier_result.has_value()) {
        score::mw::log::LogError() << "[tc8_ets_stub] Failed to create InstanceSpecifier for tc8/tc8_service";
        return 1;
    }

    score::mw::com::GenericSkeletonServiceElementInfo create_params;
    create_params.events = tc8_ets_service::kEvents;

    auto skeleton_result =
        score::mw::com::GenericSkeleton::Create(specifier_result.value(), create_params);
    if (!skeleton_result.has_value()) {
        score::mw::log::LogError() << "[tc8_ets_stub] GenericSkeleton::Create failed for tc8/tc8_service";
        return 1;
    }
    auto& skeleton = skeleton_result.value();

    const auto offer_result = skeleton.OfferService();
    if (!offer_result.has_value()) {
        score::mw::log::LogError() << "[tc8_ets_stub] OfferService() failed";
        return 1;
    }
    score::mw::log::LogInfo() << "[tc8_ets_stub] tc8_service offered, sending periodic notifications";

    auto events_view = skeleton.GetEvents();
    auto last_notify_time = std::chrono::steady_clock::now();

    while (!g_shutdown_requested.load()) {
        auto now = std::chrono::steady_clock::now();
        if (now - last_notify_time >= std::chrono::milliseconds(500)) {
            last_notify_time = now;
            for (const score::mw::com::EventInfo& ev_info : tc8_ets_service::kEvents) {
                auto it = events_view.find(ev_info.name);
                if (it == events_view.cend()) {
                    continue;
                }
                score::mw::com::GenericSkeletonEvent& event = it->second;
                auto alloc_result = event.Allocate();
                if (!alloc_result.has_value()) {
                    continue;
                }
                auto sample = std::move(alloc_result).value();
                auto* pre_data = static_cast<Payload*>(sample.Get());
                pre_data->size = tc8_ets_service::kMaxPayloadBytes;
                pre_data->data[0] = std::byte{0x01};
                (void)event.Send(std::move(sample));
            }
            score::mw::log::LogDebug() << "[tc8_ets_stub] notified all events";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    skeleton.StopOfferService();
    score::mw::log::LogInfo() << "[tc8_ets_stub] StopOfferService done; exiting";
    return 0;
}
