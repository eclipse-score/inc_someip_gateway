/********************************************************************************
 * Copyright (c) 2025 ETAS GmbH
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

#include <benchmark/benchmark.h>
#include <score/mw/com/runtime.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <iostream>
#include <mutex>
#include <optional>
#include <score/stop_token.hpp>
#include <string>
#include <thread>
#include <unordered_map>

#include "echo_service.h"

using namespace echo_service;

constexpr std::uint16_t MaxSamplesCount{10};
constexpr std::uint8_t MAX_SERVICE_DISCOVERY_RETRIES{30};
constexpr std::chrono::seconds SERVICE_DISCOVERY_RETRY_INTERVAL{1};
constexpr std::chrono::seconds SEQUENTIAL_HANDSHAKE_DELAY{2};
constexpr std::chrono::seconds RESPONSE_TIMEOUT{1};
constexpr std::uint16_t STRESS_THROUGHPUT_BATCH_SIZE{100};

constexpr const char* EchoRequestkInstanceSpecifier = "benchmark/echo_request";
constexpr const char* EchoResponseInstanceSpecifier = "benchmark/echo_response";

score::cpp::stop_source g_stop_source{score::cpp::nostopstate_t{}};
score::cpp::stop_token g_stop_token{g_stop_source.get_token()};

void SigTermHandlerFunction(int signal) {
    g_stop_source.request_stop();
    benchmark::Shutdown();
}

class BenchmarkFixture {
   public:
    static BenchmarkFixture& Instance() {
        static BenchmarkFixture instance;
        return instance;
    }

    void Initialize() {
        if (initialized_) {
            return;
        }

        std::cout << "Initializing benchmark infrastructure..." << std::endl;

        std::cout << "Looking for echo_response service..." << std::endl;

        bool service_found{false};

        for (std::uint8_t retry{0}; retry < MAX_SERVICE_DISCOVERY_RETRIES && !service_found;
             ++retry) {
            if (g_stop_token.stop_requested()) {
                throw std::runtime_error("Stop requested during service discovery");
            }

            auto response_handles_result =
                EchoResponseProxy::FindService(score::mw::com::InstanceSpecifier::Create(
                                                   std::string{EchoResponseInstanceSpecifier})
                                                   .value());

            if (response_handles_result.has_value() && !response_handles_result.value().empty()) {
                auto response_proxy_result =
                    EchoResponseProxy::Create(response_handles_result.value().front());
                if (!response_proxy_result.has_value()) {
                    throw std::runtime_error("Failed to create response proxy");
                }
                response_proxy_ = std::move(response_proxy_result).value();
                service_found = true;
                break;
            }

            if (retry == 0) {
                std::cout << "Echo response service not found. Waiting for echo_server to start..."
                          << std::endl;
                std::cout << "Please run: bazel run //tests/performance_benchmarks:echo_server"
                          << std::endl;
            }

            std::cout << "Retry " << (retry + 1) << "/" << MAX_SERVICE_DISCOVERY_RETRIES
                      << " - waiting for echo_server..." << std::endl;
            std::this_thread::sleep_for(SERVICE_DISCOVERY_RETRY_INTERVAL);
        }

        if (!service_found) {
            throw std::runtime_error("Timeout: Echo response service not found after " +
                                     std::to_string(MAX_SERVICE_DISCOVERY_RETRIES) +
                                     " seconds. Make sure echo_server is running.");
        }

        std::cout << "Subscribing to echo_response service events..." << std::endl;
        response_proxy_->echo_response_tiny_.Subscribe(MaxSamplesCount);
        response_proxy_->echo_response_small_.Subscribe(MaxSamplesCount);
        response_proxy_->echo_response_medium_.Subscribe(MaxSamplesCount);
        response_proxy_->echo_response_large_.Subscribe(MaxSamplesCount);
        response_proxy_->echo_response_xlarge_.Subscribe(MaxSamplesCount);
        response_proxy_->echo_response_xxlarge_.Subscribe(MaxSamplesCount);

        auto handler_result = response_proxy_->echo_response_tiny_.SetReceiveHandler(
            [this]() { this->ProcessResponsesTiny(); });
        auto handler_small_result = response_proxy_->echo_response_small_.SetReceiveHandler(
            [this]() { this->ProcessResponsesSmall(); });
        auto handler_medium_result = response_proxy_->echo_response_medium_.SetReceiveHandler(
            [this]() { this->ProcessResponsesMedium(); });
        auto handler_large_result = response_proxy_->echo_response_large_.SetReceiveHandler(
            [this]() { this->ProcessResponsesLarge(); });
        auto handler_xlarge_result = response_proxy_->echo_response_xlarge_.SetReceiveHandler(
            [this]() { this->ProcessResponsesXLarge(); });
        auto handler_xxlarge_result = response_proxy_->echo_response_xxlarge_.SetReceiveHandler(
            [this]() { this->ProcessResponsesXXLarge(); });

        if (!handler_result.has_value() || !handler_small_result.has_value() ||
            !handler_medium_result.has_value() || !handler_large_result.has_value() ||
            !handler_xlarge_result.has_value() || !handler_xxlarge_result.has_value()) {
            throw std::runtime_error("Failed to set response handlers");
        }

        std::cout << "Creating and offering echo_request service..." << std::endl;
        auto request_skeleton_result = EchoRequestSkeleton::Create(
            score::mw::com::InstanceSpecifier::Create(std::string{EchoRequestkInstanceSpecifier})
                .value());

        if (!request_skeleton_result.has_value()) {
            throw std::runtime_error("Failed to create request skeleton");
        }
        request_skeleton_ = std::move(request_skeleton_result).value();

        auto offer_result = request_skeleton_->OfferService();
        if (!offer_result.has_value()) {
            throw std::runtime_error("Failed to offer request service");
        }

        std::cout << "Waiting for echo server to connect..." << std::endl;
        std::this_thread::sleep_for(SEQUENTIAL_HANDSHAKE_DELAY);

        initialized_ = true;
        std::cout << "Benchmark infrastructure initialized successfully - ready to start benchmarks"
                  << std::endl;
    }

    void Cleanup() {
        if (!initialized_) {
            return;
        }

        if (response_proxy_.has_value()) {
            response_proxy_->echo_response_tiny_.UnsetReceiveHandler();
            response_proxy_->echo_response_small_.UnsetReceiveHandler();
            response_proxy_->echo_response_medium_.UnsetReceiveHandler();
            response_proxy_->echo_response_large_.UnsetReceiveHandler();
            response_proxy_->echo_response_xlarge_.UnsetReceiveHandler();
            response_proxy_->echo_response_xxlarge_.UnsetReceiveHandler();
            response_proxy_->echo_response_tiny_.Unsubscribe();
            response_proxy_->echo_response_small_.Unsubscribe();
            response_proxy_->echo_response_medium_.Unsubscribe();
            response_proxy_->echo_response_large_.Unsubscribe();
            response_proxy_->echo_response_xlarge_.Unsubscribe();
            response_proxy_->echo_response_xxlarge_.Unsubscribe();
        }

        response_proxy_.reset();
        request_skeleton_.reset();
        initialized_ = false;
        std::cout << "Benchmark infrastructure cleaned up" << std::endl;
    }

    // Send echo request and wait for response (for latency testing)
    std::chrono::nanoseconds SendEchoRequestSync(PayloadSize size) {
        auto actual_size = static_cast<std::uint32_t>(size);
        auto sequence_id = next_sequence_id_++;

        std::unique_lock<std::mutex> lock(pending_mutex_);
        pending_responses_[sequence_id] = {};

        auto send_time = std::chrono::high_resolution_clock::now();
        SendRequestUsingCorrectEvent(size, sequence_id, actual_size);

        bool received = response_cv_.wait_for(lock, RESPONSE_TIMEOUT, [this, sequence_id]() {
            return pending_responses_[sequence_id].received;
        });

        if (!received) {
            pending_responses_.erase(sequence_id);
            throw std::runtime_error(
                "Timeout waiting for echo response. Sequence ID: " + std::to_string(sequence_id) +
                ". Check if echo_server is properly handling requests.");
        }

        auto receive_time = std::chrono::high_resolution_clock::now();
        auto latency =
            std::chrono::duration_cast<std::chrono::nanoseconds>(receive_time - send_time);

        pending_responses_.erase(sequence_id);
        return latency;
    }

    // Send echo request without waiting (for throughput testing)
    void SendEchoRequestAsync(PayloadSize size) {
        auto actual_size = static_cast<std::uint32_t>(size);
        auto sequence_id = next_sequence_id_++;

        SendRequestUsingCorrectEvent(size, sequence_id, actual_size);
    }

   private:
    // Helper method to select the correct event based on payload size
    void SendRequestUsingCorrectEvent(PayloadSize size, std::uint64_t sequence_id,
                                      std::uint32_t actual_size) {
        switch (size) {
            case PayloadSize::Tiny: {
                auto request = request_skeleton_->echo_request_tiny_.Allocate().value();
                request->sequence_id = sequence_id;
                request->timestamp_ns = utils::GetCurrentTimeNanos();
                request->payload_size = size;
                request->actual_size = actual_size;
                utils::FillTestPayload(request->payload, actual_size, sequence_id);
                request_skeleton_->echo_request_tiny_.Send(std::move(request));
                break;
            }
            case PayloadSize::Small: {
                auto request = request_skeleton_->echo_request_small_.Allocate().value();
                request->sequence_id = sequence_id;
                request->timestamp_ns = utils::GetCurrentTimeNanos();
                request->payload_size = size;
                request->actual_size = actual_size;
                utils::FillTestPayload(request->payload, actual_size, sequence_id);
                request_skeleton_->echo_request_small_.Send(std::move(request));
                break;
            }
            case PayloadSize::Medium: {
                auto request = request_skeleton_->echo_request_medium_.Allocate().value();
                request->sequence_id = sequence_id;
                request->timestamp_ns = utils::GetCurrentTimeNanos();
                request->payload_size = size;
                request->actual_size = actual_size;
                utils::FillTestPayload(request->payload, actual_size, sequence_id);
                request_skeleton_->echo_request_medium_.Send(std::move(request));
                break;
            }
            case PayloadSize::Large: {
                auto request = request_skeleton_->echo_request_large_.Allocate().value();
                request->sequence_id = sequence_id;
                request->timestamp_ns = utils::GetCurrentTimeNanos();
                request->payload_size = size;
                request->actual_size = actual_size;
                utils::FillTestPayload(request->payload, actual_size, sequence_id);
                request_skeleton_->echo_request_large_.Send(std::move(request));
                break;
            }
            case PayloadSize::XLarge: {
                auto request = request_skeleton_->echo_request_xlarge_.Allocate().value();
                request->sequence_id = sequence_id;
                request->timestamp_ns = utils::GetCurrentTimeNanos();
                request->payload_size = size;
                request->actual_size = actual_size;
                utils::FillTestPayload(request->payload, actual_size, sequence_id);
                request_skeleton_->echo_request_xlarge_.Send(std::move(request));
                break;
            }
            case PayloadSize::XXLarge: {
                auto request = request_skeleton_->echo_request_xxlarge_.Allocate().value();
                request->sequence_id = sequence_id;
                request->timestamp_ns = utils::GetCurrentTimeNanos();
                request->payload_size = size;
                request->actual_size = actual_size;
                utils::FillTestPayload(request->payload, actual_size, sequence_id);
                request_skeleton_->echo_request_xxlarge_.Send(std::move(request));
                break;
            }
        }
    }

    struct PendingResponse {
        bool received{false};
        std::chrono::high_resolution_clock::time_point receive_time;
    };

    void ProcessResponsesTiny() {
        if (g_stop_token.stop_requested()) {
            return;
        }

        response_proxy_->echo_response_tiny_.GetNewSamples(
            [this](const auto& response_sample) {
                if (g_stop_token.stop_requested()) {
                    return;
                }

                std::lock_guard<std::mutex> lock(pending_mutex_);
                auto it = pending_responses_.find(response_sample->sequence_id);
                if (it != pending_responses_.end()) {
                    it->second.received = true;
                    it->second.receive_time = std::chrono::high_resolution_clock::now();
                    response_cv_.notify_all();
                }
            },
            MaxSamplesCount);
    }

    void ProcessResponsesSmall() {
        if (g_stop_token.stop_requested()) {
            return;
        }

        response_proxy_->echo_response_small_.GetNewSamples(
            [this](const auto& response_sample) {
                if (g_stop_token.stop_requested()) {
                    return;
                }

                std::lock_guard<std::mutex> lock(pending_mutex_);
                auto it = pending_responses_.find(response_sample->sequence_id);
                if (it != pending_responses_.end()) {
                    it->second.received = true;
                    it->second.receive_time = std::chrono::high_resolution_clock::now();
                    response_cv_.notify_all();
                }
            },
            MaxSamplesCount);
    }

    void ProcessResponsesMedium() {
        if (g_stop_token.stop_requested()) {
            return;
        }

        response_proxy_->echo_response_medium_.GetNewSamples(
            [this](const auto& response_sample) {
                if (g_stop_token.stop_requested()) {
                    return;
                }

                std::lock_guard<std::mutex> lock(pending_mutex_);
                auto it = pending_responses_.find(response_sample->sequence_id);
                if (it != pending_responses_.end()) {
                    it->second.received = true;
                    it->second.receive_time = std::chrono::high_resolution_clock::now();
                    response_cv_.notify_all();
                }
            },
            MaxSamplesCount);
    }

    void ProcessResponsesLarge() {
        if (g_stop_token.stop_requested()) {
            return;
        }

        response_proxy_->echo_response_large_.GetNewSamples(
            [this](const auto& response_sample) {
                if (g_stop_token.stop_requested()) {
                    return;
                }

                std::lock_guard<std::mutex> lock(pending_mutex_);
                auto it = pending_responses_.find(response_sample->sequence_id);
                if (it != pending_responses_.end()) {
                    it->second.received = true;
                    it->second.receive_time = std::chrono::high_resolution_clock::now();
                    response_cv_.notify_all();
                }
            },
            MaxSamplesCount);
    }

    void ProcessResponsesXLarge() {
        if (g_stop_token.stop_requested()) {
            return;
        }

        response_proxy_->echo_response_xlarge_.GetNewSamples(
            [this](const auto& response_sample) {
                if (g_stop_token.stop_requested()) {
                    return;
                }

                std::lock_guard<std::mutex> lock(pending_mutex_);
                auto it = pending_responses_.find(response_sample->sequence_id);
                if (it != pending_responses_.end()) {
                    it->second.received = true;
                    it->second.receive_time = std::chrono::high_resolution_clock::now();
                    response_cv_.notify_all();
                }
            },
            MaxSamplesCount);
    }

    void ProcessResponsesXXLarge() {
        if (g_stop_token.stop_requested()) {
            return;
        }

        response_proxy_->echo_response_xxlarge_.GetNewSamples(
            [this](const auto& response_sample) {
                if (g_stop_token.stop_requested()) {
                    return;
                }

                std::lock_guard<std::mutex> lock(pending_mutex_);
                auto it = pending_responses_.find(response_sample->sequence_id);
                if (it != pending_responses_.end()) {
                    it->second.received = true;
                    it->second.receive_time = std::chrono::high_resolution_clock::now();
                    response_cv_.notify_all();
                }
            },
            MaxSamplesCount);
    }

    bool initialized_{false};
    std::atomic<std::uint64_t> next_sequence_id_{1};

    std::optional<EchoRequestSkeleton> request_skeleton_;
    std::optional<EchoResponseProxy> response_proxy_;

    std::mutex pending_mutex_;
    std::condition_variable response_cv_;
    std::unordered_map<std::uint64_t, PendingResponse> pending_responses_;
};

class IpcBenchmark : public benchmark::Fixture {
   public:
    void SetUp(const ::benchmark::State& state) override {
        if (!setup_done_) {
            BenchmarkFixture::Instance().Initialize();
            setup_done_ = true;
        }
    }

    void TearDown(const ::benchmark::State& state) override {
        // Cleanup is done in global teardown
    }

   private:
    static bool setup_done_;
};

bool IpcBenchmark::setup_done_{false};

struct PayloadConfig {
    PayloadSize size;
    const char* name;
};

constexpr PayloadConfig PAYLOAD_CONFIGS[] = {
    {PayloadSize::Tiny, "Tiny_8B"},       {PayloadSize::Small, "Small_64B"},
    {PayloadSize::Medium, "Medium_1KB"},  {PayloadSize::Large, "Large_8KB"},
    {PayloadSize::XLarge, "XLarge_64KB"}, {PayloadSize::XXLarge, "XXLarge_1MB"}};

constexpr size_t NUM_PAYLOAD_CONFIGS = sizeof(PAYLOAD_CONFIGS) / sizeof(PAYLOAD_CONFIGS[0]);

PayloadSize GetPayloadSizeFromArg(int64_t arg) {
    if (arg >= 0 && arg < static_cast<int64_t>(NUM_PAYLOAD_CONFIGS)) {
        return PAYLOAD_CONFIGS[arg].size;
    }
    return PayloadSize::Small;  // Default fallback
}

std::string GetPayloadSizeName(PayloadSize size) {
    for (const auto& config : PAYLOAD_CONFIGS) {
        if (config.size == size) {
            return config.name;
        }
    }
    return "Unknown";
}

// Latency benchmarks - measure round-trip time
BENCHMARK_DEFINE_F(IpcBenchmark, LatencyEcho)(benchmark::State& state) {
    auto payload_size = GetPayloadSizeFromArg(state.range(0));

    for (auto _ : state) {
        auto latency = BenchmarkFixture::Instance().SendEchoRequestSync(payload_size);
        state.SetIterationTime(latency.count() / 1e9);  // Convert nanoseconds to seconds
    }

    state.SetLabel(GetPayloadSizeName(payload_size));
    state.counters["payload_bytes"] = static_cast<double>(static_cast<std::uint32_t>(payload_size));
}

BENCHMARK_REGISTER_F(IpcBenchmark, LatencyEcho)
    ->Arg(0)  // Tiny
    ->Arg(1)  // Small
    ->Arg(2)  // Medium
    ->Arg(3)  // Large
    ->Arg(4)  // XLarge
    ->Arg(5)  // XXLarge
    ->UseManualTime()
    ->Unit(benchmark::kMicrosecond);

// Throughput benchmarks - measure message sending rate
BENCHMARK_DEFINE_F(IpcBenchmark, ThroughputEcho)(benchmark::State& state) {
    auto payload_size = GetPayloadSizeFromArg(state.range(0));
    auto payload_bytes = static_cast<std::uint32_t>(payload_size);

    for (auto _ : state) {
        BenchmarkFixture::Instance().SendEchoRequestAsync(payload_size);
    }

    state.SetLabel(GetPayloadSizeName(payload_size));
    state.counters["payload_bytes"] = static_cast<double>(payload_bytes);
    state.counters["messages_per_sec"] =
        benchmark::Counter(static_cast<double>(state.iterations()), benchmark::Counter::kIsRate);
    state.counters["bytes_per_sec"] = benchmark::Counter(
        static_cast<double>(state.iterations() * payload_bytes), benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(IpcBenchmark, ThroughputEcho)
    ->Arg(0)  // Tiny
    ->Arg(1)  // Small
    ->Arg(2)  // Medium
    ->Arg(3)  // Large
    ->Arg(4)  // XLarge
    ->Arg(5)  // XXLarge
    ->Unit(benchmark::kMicrosecond);

// Stress test - send messages in batches to test system under high load
BENCHMARK_DEFINE_F(IpcBenchmark, StressThroughput)(benchmark::State& state) {
    auto payload_size = GetPayloadSizeFromArg(state.range(0));
    auto payload_bytes = static_cast<std::uint32_t>(payload_size);

    for (auto _ : state) {
        for (std::uint16_t i{0}; i < STRESS_THROUGHPUT_BATCH_SIZE; ++i) {
            BenchmarkFixture::Instance().SendEchoRequestAsync(payload_size);
        }
    }

    auto batch_name =
        GetPayloadSizeName(payload_size) + "_Batch" + std::to_string(STRESS_THROUGHPUT_BATCH_SIZE);
    state.SetLabel(batch_name);
    state.counters["payload_bytes"] = static_cast<double>(payload_bytes);
    state.counters["messages_per_sec"] =
        benchmark::Counter(static_cast<double>(state.iterations() * STRESS_THROUGHPUT_BATCH_SIZE),
                           benchmark::Counter::kIsRate);
    state.counters["bytes_per_sec"] = benchmark::Counter(
        static_cast<double>(state.iterations() * STRESS_THROUGHPUT_BATCH_SIZE * payload_bytes),
        benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(IpcBenchmark, StressThroughput)
    ->Arg(0)  // Tiny
    ->Arg(1)  // Small
    ->Arg(2)  // Medium
    ->Arg(3)  // Large
    ->Unit(benchmark::kMicrosecond);

int main(int argc, char** argv) {
    std::signal(SIGINT, SigTermHandlerFunction);
    std::signal(SIGTERM, SigTermHandlerFunction);

    g_stop_source = score::cpp::stop_source{};
    g_stop_token = g_stop_source.get_token();

    // Initialize runtime with default config
    const char* score_args[] = {"ipc_benchmarks", "-service_instance_manifest",
                                "tests/performance_benchmarks/config/benchmark_mw_com_config.json"};
    int score_argc = sizeof(score_args) / sizeof(score_args[0]);
    score::mw::com::runtime::InitializeRuntime(score_argc, score_args);

    benchmark::Initialize(&argc, argv);

    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }

    std::cout << "Starting IPC Performance Benchmarks..." << std::endl;
    std::cout << "Echo server should be running. If not, run:" << std::endl;
    std::cout << "bazel run //tests/performance_benchmarks:echo_server" << std::endl;

    if (g_stop_token.stop_requested()) {
        std::cout << "Stop requested before running benchmarks. Exiting..." << std::endl;
        return 0;
    }

    benchmark::RunSpecifiedBenchmarks();

    BenchmarkFixture::Instance().Cleanup();

    return 0;
}
