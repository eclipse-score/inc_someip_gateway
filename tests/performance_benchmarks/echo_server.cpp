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
// SPDX-FileCopyrightText: 2025, 2026 Contributors to the Eclipse Foundation
// SPDX-License-Identifier: Apache-2.0
// *******************************************************************************

#include <chrono>
#include <csignal>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "echo_service.h"
#include "score/mw/com/runtime.h"
#include "score/stop_token.hpp"

using namespace echo_service;

static std::size_t total_processed{0};

constexpr std::uint16_t MaxSamplesCount{10};
constexpr std::size_t LOAD_BALANCING_INTERVAL{1000};
constexpr std::chrono::milliseconds LOAD_BALANCING_DELAY{1};
constexpr std::chrono::milliseconds MAIN_LOOP_SLEEP{50};
constexpr std::chrono::seconds INITIAL_CLIENT_WAIT{2};
constexpr std::chrono::seconds STATS_INTERVAL{5};

constexpr const char* EchoRequestInstanceSpecifier = "benchmark/echo_request";
constexpr const char* EchoResponseInstanceSpecifier = "benchmark/echo_response";

score::cpp::stop_source g_stop_source{score::cpp::nostopstate_t{}};
score::cpp::stop_token g_stop_token{g_stop_source.get_token()};

void SigTermHandlerFunction(int signal) { g_stop_source.request_stop(); }

static std::optional<EchoRequestProxy> TryConnectToClient() {
    auto handles = EchoRequestProxy::FindService(
        score::mw::com::InstanceSpecifier::Create(std::string{EchoRequestInstanceSpecifier})
            .value());

    if (!handles.has_value() || handles.value().empty()) {
        return std::nullopt;
    }

    auto proxy_result = EchoRequestProxy::Create(handles.value().front());
    if (!proxy_result.has_value()) {
        return std::nullopt;
    }

    return std::move(proxy_result).value();
}

template <typename RequestSample>
static void ProcessSingleEchoRequestTiny(const RequestSample& request_sample,
                                         EchoResponseSkeleton& response_skeleton,
                                         std::size_t& requests_processed) {
    if (g_stop_token.stop_requested()) {
        return;
    }

    auto response_result = response_skeleton.echo_response_tiny_.Allocate();
    if (!response_result.has_value()) {
        std::cerr << "Failed to allocate tiny response for sequence_id: "
                  << request_sample->sequence_id << std::endl;
        return;
    }

    auto response = std::move(response_result).value();
    utils::CopyMessageForEcho(*response, *request_sample);

    auto send_result = response_skeleton.echo_response_tiny_.Send(std::move(response));
    if (!send_result.has_value()) {
        std::cerr << "Failed to send tiny response for sequence_id: " << request_sample->sequence_id
                  << std::endl;
        return;
    }

    ++requests_processed;
    ++total_processed;

    if (total_processed % LOAD_BALANCING_INTERVAL == 0) {
        std::this_thread::sleep_for(LOAD_BALANCING_DELAY);
    }
}

template <typename RequestSample>
static void ProcessSingleEchoRequestSmall(const RequestSample& request_sample,
                                          EchoResponseSkeleton& response_skeleton,
                                          std::size_t& requests_processed) {
    if (g_stop_token.stop_requested()) {
        return;
    }

    auto response_result = response_skeleton.echo_response_small_.Allocate();
    if (!response_result.has_value()) {
        std::cerr << "Failed to allocate small response for sequence_id: "
                  << request_sample->sequence_id << std::endl;
        return;
    }

    auto response = std::move(response_result).value();
    utils::CopyMessageForEcho(*response, *request_sample);

    auto send_result = response_skeleton.echo_response_small_.Send(std::move(response));
    if (!send_result.has_value()) {
        std::cerr << "Failed to send small response for sequence_id: "
                  << request_sample->sequence_id << std::endl;
        return;
    }

    ++requests_processed;
    ++total_processed;

    if (total_processed % LOAD_BALANCING_INTERVAL == 0) {
        std::this_thread::sleep_for(LOAD_BALANCING_DELAY);
    }
}

template <typename RequestSample>
static void ProcessSingleEchoRequestMedium(const RequestSample& request_sample,
                                           EchoResponseSkeleton& response_skeleton,
                                           std::size_t& requests_processed) {
    if (g_stop_token.stop_requested()) {
        return;
    }

    auto response_result = response_skeleton.echo_response_medium_.Allocate();
    if (!response_result.has_value()) {
        std::cerr << "Failed to allocate medium response for sequence_id: "
                  << request_sample->sequence_id << std::endl;
        return;
    }

    auto response = std::move(response_result).value();
    utils::CopyMessageForEcho(*response, *request_sample);

    auto send_result = response_skeleton.echo_response_medium_.Send(std::move(response));
    if (!send_result.has_value()) {
        std::cerr << "Failed to send medium response for sequence_id: "
                  << request_sample->sequence_id << std::endl;
        return;
    }

    ++requests_processed;
    ++total_processed;

    if (total_processed % LOAD_BALANCING_INTERVAL == 0) {
        std::this_thread::sleep_for(LOAD_BALANCING_DELAY);
    }
}

template <typename RequestSample>
static void ProcessSingleEchoRequestLarge(const RequestSample& request_sample,
                                          EchoResponseSkeleton& response_skeleton,
                                          std::size_t& requests_processed) {
    if (g_stop_token.stop_requested()) {
        return;
    }

    auto response_result = response_skeleton.echo_response_large_.Allocate();
    if (!response_result.has_value()) {
        std::cerr << "Failed to allocate large response for sequence_id: "
                  << request_sample->sequence_id << std::endl;
        return;
    }

    auto response = std::move(response_result).value();
    utils::CopyMessageForEcho(*response, *request_sample);

    auto send_result = response_skeleton.echo_response_large_.Send(std::move(response));
    if (!send_result.has_value()) {
        std::cerr << "Failed to send large response for sequence_id: "
                  << request_sample->sequence_id << std::endl;
        return;
    }

    ++requests_processed;
    ++total_processed;

    if (total_processed % LOAD_BALANCING_INTERVAL == 0) {
        std::this_thread::sleep_for(LOAD_BALANCING_DELAY);
    }
}

template <typename RequestSample>
static void ProcessSingleEchoRequestXLarge(const RequestSample& request_sample,
                                           EchoResponseSkeleton& response_skeleton,
                                           std::size_t& requests_processed) {
    if (g_stop_token.stop_requested()) {
        return;
    }

    auto response_result = response_skeleton.echo_response_xlarge_.Allocate();
    if (!response_result.has_value()) {
        std::cerr << "Failed to allocate xlarge response for sequence_id: "
                  << request_sample->sequence_id << std::endl;
        return;
    }

    auto response = std::move(response_result).value();
    utils::CopyMessageForEcho(*response, *request_sample);

    auto send_result = response_skeleton.echo_response_xlarge_.Send(std::move(response));
    if (!send_result.has_value()) {
        std::cerr << "Failed to send xlarge response for sequence_id: "
                  << request_sample->sequence_id << std::endl;
        return;
    }

    ++requests_processed;
    ++total_processed;

    if (total_processed % LOAD_BALANCING_INTERVAL == 0) {
        std::this_thread::sleep_for(LOAD_BALANCING_DELAY);
    }
}

template <typename RequestSample>
static void ProcessSingleEchoRequestXXLarge(const RequestSample& request_sample,
                                            EchoResponseSkeleton& response_skeleton,
                                            std::size_t& requests_processed) {
    if (g_stop_token.stop_requested()) {
        return;
    }

    auto response_result = response_skeleton.echo_response_xxlarge_.Allocate();
    if (!response_result.has_value()) {
        std::cerr << "Failed to allocate xxlarge response for sequence_id: "
                  << request_sample->sequence_id << std::endl;
        return;
    }

    auto response = std::move(response_result).value();
    utils::CopyMessageForEcho(*response, *request_sample);

    auto send_result = response_skeleton.echo_response_xxlarge_.Send(std::move(response));
    if (!send_result.has_value()) {
        std::cerr << "Failed to send xxlarge response for sequence_id: "
                  << request_sample->sequence_id << std::endl;
        return;
    }

    ++requests_processed;
    ++total_processed;

    if (total_processed % LOAD_BALANCING_INTERVAL == 0) {
        std::this_thread::sleep_for(LOAD_BALANCING_DELAY);
    }
}

static void ProcessEchoRequests(
    EchoRequestProxy& request_proxy, EchoResponseSkeleton& response_skeleton,
    std::size_t& requests_processed_tiny, std::size_t& requests_processed_small,
    std::size_t& requests_processed_medium, std::size_t& requests_processed_large,
    std::size_t& requests_processed_xlarge, std::size_t& requests_processed_xxlarge) {
    if (g_stop_token.stop_requested()) {
        return;
    }

    request_proxy.echo_request_tiny_.GetNewSamples(
        [&](const auto& request_sample) {
            ProcessSingleEchoRequestTiny(request_sample, response_skeleton,
                                         requests_processed_tiny);
        },
        MaxSamplesCount);

    request_proxy.echo_request_small_.GetNewSamples(
        [&](const auto& request_sample) {
            ProcessSingleEchoRequestSmall(request_sample, response_skeleton,
                                          requests_processed_small);
        },
        MaxSamplesCount);

    request_proxy.echo_request_medium_.GetNewSamples(
        [&](const auto& request_sample) {
            ProcessSingleEchoRequestMedium(request_sample, response_skeleton,
                                           requests_processed_medium);
        },
        MaxSamplesCount);

    request_proxy.echo_request_large_.GetNewSamples(
        [&](const auto& request_sample) {
            ProcessSingleEchoRequestLarge(request_sample, response_skeleton,
                                          requests_processed_large);
        },
        MaxSamplesCount);

    request_proxy.echo_request_xlarge_.GetNewSamples(
        [&](const auto& request_sample) {
            ProcessSingleEchoRequestXLarge(request_sample, response_skeleton,
                                           requests_processed_xlarge);
        },
        MaxSamplesCount);

    request_proxy.echo_request_xxlarge_.GetNewSamples(
        [&](const auto& request_sample) {
            ProcessSingleEchoRequestXXLarge(request_sample, response_skeleton,
                                            requests_processed_xxlarge);
        },
        MaxSamplesCount);
}

enum class ServerState { WaitingForClient, SettingUpHandler, ProcessingRequests };

int main(int argc, const char* argv[]) {
    std::signal(SIGINT, SigTermHandlerFunction);
    std::signal(SIGTERM, SigTermHandlerFunction);

    g_stop_source = score::cpp::stop_source{};
    g_stop_token = g_stop_source.get_token();

    std::cout << "Starting Echo Server..." << std::endl;

    score::mw::com::runtime::InitializeRuntime(argc, argv);

    auto response_skeleton_result = EchoResponseSkeleton::Create(
        score::mw::com::InstanceSpecifier::Create(std::string{EchoResponseInstanceSpecifier})
            .value());

    if (!response_skeleton_result.has_value()) {
        std::cerr << "Failed to create response skeleton" << std::endl;
        return 1;
    }
    auto response_skeleton = std::move(response_skeleton_result).value();

    auto offer_result = response_skeleton.OfferService();
    if (!offer_result.has_value()) {
        std::cerr << "Failed to offer response service" << std::endl;
        return 1;
    }

    std::cout << "Echo Server ready - listening for requests..." << std::endl;

    std::size_t requests_processed_tiny{0};
    std::size_t requests_processed_small{0};
    std::size_t requests_processed_medium{0};
    std::size_t requests_processed_large{0};
    std::size_t requests_processed_xlarge{0};
    std::size_t requests_processed_xxlarge{0};
    auto last_stats_time = std::chrono::steady_clock::now();

    ServerState current_state = ServerState::WaitingForClient;
    std::optional<EchoRequestProxy> request_proxy;

    // Give some time for the benchmark client to start and subscribe
    std::this_thread::sleep_for(INITIAL_CLIENT_WAIT);

    while (!g_stop_token.stop_requested()) {
        switch (current_state) {
            case ServerState::WaitingForClient: {
                if (!request_proxy.has_value()) {
                    auto connection_result = TryConnectToClient();
                    if (connection_result.has_value()) {
                        request_proxy = std::move(connection_result).value();
                        std::cout << "Benchmark client connected" << std::endl;
                        current_state = ServerState::SettingUpHandler;
                    }
                }
                break;
            }
            case ServerState::SettingUpHandler: {
                if (request_proxy.has_value()) {
                    request_proxy->echo_request_tiny_.Subscribe(MaxSamplesCount);
                    request_proxy->echo_request_small_.Subscribe(MaxSamplesCount);
                    request_proxy->echo_request_medium_.Subscribe(MaxSamplesCount);
                    request_proxy->echo_request_large_.Subscribe(MaxSamplesCount);
                    request_proxy->echo_request_xlarge_.Subscribe(MaxSamplesCount);
                    request_proxy->echo_request_xxlarge_.Subscribe(MaxSamplesCount);

                    std::cout << "All request handlers setup complete" << std::endl;
                    current_state = ServerState::ProcessingRequests;
                } else {
                    current_state = ServerState::WaitingForClient;
                }
                break;
            }
            case ServerState::ProcessingRequests: {
                if (request_proxy.has_value()) {
                    ProcessEchoRequests(*request_proxy, response_skeleton, requests_processed_tiny,
                                        requests_processed_small, requests_processed_medium,
                                        requests_processed_large, requests_processed_xlarge,
                                        requests_processed_xxlarge);
                }
                break;
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (now - last_stats_time >= STATS_INTERVAL) {
            switch (current_state) {
                case ServerState::WaitingForClient:
                    std::cout << "Waiting for benchmark clients to connect..." << std::endl;
                    break;
                case ServerState::SettingUpHandler:
                    std::cout << "Connected to benchmark clients, setting up handlers..."
                              << std::endl;
                    break;
                case ServerState::ProcessingRequests:
                    std::cout << "Processed requests - Tiny: " << requests_processed_tiny
                              << ", Small: " << requests_processed_small
                              << ", Medium: " << requests_processed_medium
                              << ", Large: " << requests_processed_large
                              << ", XLarge: " << requests_processed_xlarge
                              << ", XXLarge: " << requests_processed_xxlarge << std::endl;
                    break;
            }
            last_stats_time = now;
        }

        // Sleep briefly to avoid busy waiting
        std::this_thread::sleep_for(MAIN_LOOP_SLEEP);
    }

    if (request_proxy.has_value()) {
        request_proxy->echo_request_tiny_.Unsubscribe();
        request_proxy->echo_request_small_.Unsubscribe();
        request_proxy->echo_request_medium_.Unsubscribe();
        request_proxy->echo_request_large_.Unsubscribe();
        request_proxy->echo_request_xlarge_.Unsubscribe();
        request_proxy->echo_request_xxlarge_.Unsubscribe();
    }

    auto total_requests =
        requests_processed_tiny + requests_processed_small + requests_processed_medium;
    std::cout << "Echo Server shutdown complete. Total requests processed: " << total_requests
              << " (Tiny: " << requests_processed_tiny << ", Small: " << requests_processed_small
              << ", Medium: " << requests_processed_medium
              << ", Large: " << requests_processed_large
              << ", XLarge: " << requests_processed_xlarge
              << ", XXLarge: " << requests_processed_xxlarge << ")" << std::endl;

    return 0;
}
