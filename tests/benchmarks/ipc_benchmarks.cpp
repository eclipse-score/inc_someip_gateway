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

#include <benchmark/benchmark.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "echo_service.h"
#include "score/mw/com/runtime.h"
#include "score/stop_token.hpp"

using namespace echo_service;
using namespace std::chrono_literals;

constexpr std::uint16_t MaxSamplesCount{10};
constexpr std::uint8_t MAX_SERVICE_DISCOVERY_RETRIES{30};
constexpr auto SERVICE_DISCOVERY_RETRY_INTERVAL{1s};
constexpr auto SEQUENTIAL_HANDSHAKE_DELAY{2s};
constexpr auto RESPONSE_TIMEOUT{1s};
constexpr auto POLLING_INTERVAL{10us};
constexpr std::uint16_t STRESS_THROUGHPUT_BATCH_SIZE{100};

constexpr const char* EchoRequestkInstanceSpecifier = "benchmark/echo_request";
constexpr const char* EchoResponseInstanceSpecifier = "benchmark/echo_response";

namespace {
score::cpp::stop_source g_stop_source{score::cpp::nostopstate_t{}};
score::cpp::stop_token g_stop_token{g_stop_source.get_token()};

void SigTermHandlerFunction(int /*signal*/) {
    g_stop_source.request_stop();
    benchmark::Shutdown();
}

// Wraps the default console reporter to detect whether any benchmark called State::SkipWithError().
class ErrorTrackingReporter : public benchmark::ConsoleReporter {
   public:
    void ReportRuns(const std::vector<Run>& reports) override {
        for (const auto& run : reports) {
            if (run.skipped == benchmark::internal::SkippedWithError) {
                had_error_ = true;
            }
        }
        ConsoleReporter::ReportRuns(reports);
    }

    bool HadError() const { return had_error_; }

   private:
    bool had_error_{false};
};

}  // namespace

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

            auto response_specifier = score::mw::com::InstanceSpecifier::Create(
                std::string{EchoResponseInstanceSpecifier});
            if (!response_specifier.has_value()) {
                throw std::runtime_error(
                    "Failed to create the echo response instance specifier from the manifest");
            }
            auto response_handles_result =
                EchoResponsePreSerializedProxy::FindService(response_specifier.value());

            if (response_handles_result.has_value() && !response_handles_result.value().empty()) {
                auto response_proxy_result =
                    EchoResponsePreSerializedProxy::Create(response_handles_result.value().front());
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
                std::cout << "Please run: bazel run //tests/benchmarks:echo_server" << std::endl;
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

        // auto handler_result = response_proxy_->echo_response_tiny_.SetReceiveHandler(
        //     [this]() {
        //     this->ProcessResponses<EchoResponseTiny>(response_proxy_->echo_response_tiny_); });
        auto handler_small_result =
            response_proxy_->echo_response_small_.SetReceiveHandler([this]() {
                this->ProcessResponses<EchoResponseSmall>(response_proxy_->echo_response_small_);
            });
        auto handler_medium_result =
            response_proxy_->echo_response_medium_.SetReceiveHandler([this]() {
                this->ProcessResponses<EchoResponseMedium>(response_proxy_->echo_response_medium_);
            });
        auto handler_large_result =
            response_proxy_->echo_response_large_.SetReceiveHandler([this]() {
                this->ProcessResponses<EchoResponseLarge>(response_proxy_->echo_response_large_);
            });
        auto handler_xlarge_result =
            response_proxy_->echo_response_xlarge_.SetReceiveHandler([this]() {
                this->ProcessResponses<EchoResponseXLarge>(response_proxy_->echo_response_xlarge_);
            });
        auto handler_xxlarge_result =
            response_proxy_->echo_response_xxlarge_.SetReceiveHandler([this]() {
                this->ProcessResponses<EchoResponseXXLarge>(
                    response_proxy_->echo_response_xxlarge_);
            });

        if (/* !handler_result.has_value() || */ !handler_small_result.has_value() ||
            !handler_medium_result.has_value() || !handler_large_result.has_value() ||
            !handler_xlarge_result.has_value() || !handler_xxlarge_result.has_value()) {
            throw std::runtime_error("Failed to set response handlers");
        }

        std::cout << "Subscribing to echo_response service events..." << std::endl;
        (void)response_proxy_->echo_response_tiny_.Subscribe(MaxSamplesCount);
        (void)response_proxy_->echo_response_small_.Subscribe(MaxSamplesCount);
        (void)response_proxy_->echo_response_medium_.Subscribe(MaxSamplesCount);
        (void)response_proxy_->echo_response_large_.Subscribe(MaxSamplesCount);
        (void)response_proxy_->echo_response_xlarge_.Subscribe(MaxSamplesCount);
        (void)response_proxy_->echo_response_xxlarge_.Subscribe(MaxSamplesCount);

        std::cout << "Creating and offering echo_request service..." << std::endl;
        auto request_specifier =
            score::mw::com::InstanceSpecifier::Create(std::string{EchoRequestkInstanceSpecifier});
        if (!request_specifier.has_value()) {
            throw std::runtime_error(
                "Failed to create the echo request instance specifier from the manifest");
        }
        auto request_skeleton_result =
            EchoRequestPreSerializedSkeleton::Create(request_specifier.value());

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
            (void)response_proxy_->echo_response_tiny_.UnsetReceiveHandler();
            (void)response_proxy_->echo_response_small_.UnsetReceiveHandler();
            (void)response_proxy_->echo_response_medium_.UnsetReceiveHandler();
            (void)response_proxy_->echo_response_large_.UnsetReceiveHandler();
            (void)response_proxy_->echo_response_xlarge_.UnsetReceiveHandler();
            (void)response_proxy_->echo_response_xxlarge_.UnsetReceiveHandler();
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

        auto send_time = std::chrono::high_resolution_clock::now();
        SendRequestUsingCorrectEvent(size, sequence_id, actual_size);

        if (true) {
            // Use polling for tiny events
            return SendEchoRequestSyncWithPolling(sequence_id, send_time);
        } else {
            // Use handler-based approach for other events
            std::unique_lock<std::mutex> lock(pending_mutex_);
            pending_responses_[sequence_id] = {};

            bool received = response_cv_.wait_for(lock, RESPONSE_TIMEOUT, [this, sequence_id]() {
                return pending_responses_[sequence_id].received;
            });

            if (!received) {
                pending_responses_.erase(sequence_id);
                throw std::runtime_error("Timeout waiting for echo response. Sequence ID: " +
                                         std::to_string(sequence_id) +
                                         ". Check if echo_server is properly handling requests.");
            }

            auto receive_time = std::chrono::high_resolution_clock::now();
            auto latency =
                std::chrono::duration_cast<std::chrono::nanoseconds>(receive_time - send_time);

            pending_responses_.erase(sequence_id);
            return latency;
        }
    }

    // Send echo request without waiting (for throughput testing)
    void SendEchoRequestAsync(PayloadSize size) {
        auto actual_size = static_cast<std::uint32_t>(size);
        auto sequence_id = next_sequence_id_++;

        SendRequestUsingCorrectEvent(size, sequence_id, actual_size);
    }

   private:
    std::chrono::nanoseconds SendEchoRequestSyncWithPolling(
        std::uint64_t sequence_id, std::chrono::high_resolution_clock::time_point send_time) {
        auto start_time = std::chrono::high_resolution_clock::now();

        while (std::chrono::high_resolution_clock::now() - start_time < RESPONSE_TIMEOUT) {
            if (g_stop_token.stop_requested()) {
                std::cout << "Stop requested during polling for sequence_id: " << sequence_id
                          << std::endl;
                return std::chrono::nanoseconds{0};
            }

            bool found = false;
            std::chrono::high_resolution_clock::time_point receive_time;

            (void)response_proxy_->echo_response_tiny_.GetNewSamples(
                [&](auto pre_serialized_response_sample) {
                    static_assert(
                        sizeof(EchoResponseTiny) <=
                            decltype(pre_serialized_response_sample)::element_type::kMaxMessageSize,
                        "EchoResponseTiny size exceeds max sample count");
                    assert(pre_serialized_response_sample->size == sizeof(EchoResponseTiny));
                    const auto* response_sample = reinterpret_cast<const EchoResponseTiny*>(
                        pre_serialized_response_sample->data);

                    if (response_sample->sequence_id == sequence_id) {
                        receive_time = std::chrono::high_resolution_clock::now();
                        found = true;
                    }
                },
                MaxSamplesCount);

            if (found) {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(receive_time -
                                                                            send_time);
            }

            // Small delay to avoid busy waiting
            std::this_thread::sleep_for(POLLING_INTERVAL);
        }

        std::cout << "Timeout waiting for echo response with polling. Sequence ID: " << sequence_id
                  << ". Check if echo_server is properly handling requests." << std::endl;
        return std::chrono::nanoseconds{0};
    }

    template <typename RequestType, typename EventType>
    void SendRequest(EventType& request_event, PayloadSize size, std::uint64_t sequence_id,
                     std::uint32_t actual_size) {
        auto pre_serialized_request = request_event.Allocate().value();
        pre_serialized_request->size = sizeof(RequestType);
        auto* request = reinterpret_cast<RequestType*>(pre_serialized_request->data);
        request->sequence_id = sequence_id;
        request->timestamp_ns = utils::GetCurrentTimeNanos();
        request->payload_size = size;
        request->actual_size = actual_size;
        utils::FillTestPayload(request->payload, actual_size, sequence_id);
        (void)request_event.Send(std::move(pre_serialized_request));
    }

    // Helper method to select the correct event based on payload size
    void SendRequestUsingCorrectEvent(PayloadSize size, std::uint64_t sequence_id,
                                      std::uint32_t actual_size) {
        switch (size) {
            case PayloadSize::Tiny:
                SendRequest<EchoRequestTiny>(request_skeleton_->echo_request_tiny_, size,
                                             sequence_id, actual_size);
                break;
            case PayloadSize::Small:
                SendRequest<EchoRequestSmall>(request_skeleton_->echo_request_small_, size,
                                              sequence_id, actual_size);
                break;
            case PayloadSize::Medium:
                SendRequest<EchoRequestMedium>(request_skeleton_->echo_request_medium_, size,
                                               sequence_id, actual_size);
                break;
            case PayloadSize::Large:
                SendRequest<EchoRequestLarge>(request_skeleton_->echo_request_large_, size,
                                              sequence_id, actual_size);
                break;
            case PayloadSize::XLarge:
                SendRequest<EchoRequestXLarge>(request_skeleton_->echo_request_xlarge_, size,
                                               sequence_id, actual_size);
                break;
            case PayloadSize::XXLarge:
                SendRequest<EchoRequestXXLarge>(request_skeleton_->echo_request_xxlarge_, size,
                                                sequence_id, actual_size);
                break;
        }
    }

    struct PendingResponse {
        bool received{false};
        std::chrono::high_resolution_clock::time_point receive_time;
    };

    template <typename ResponseType, typename EventType>
    void ProcessResponses(EventType& response_event) {
        if (g_stop_token.stop_requested()) {
            return;
        }

        (void)response_event.GetNewSamples(
            [this](auto pre_serialized_response_sample) {
                if (g_stop_token.stop_requested()) {
                    return;
                }

                assert(pre_serialized_response_sample->size == sizeof(ResponseType));
                auto* response_sample =
                    reinterpret_cast<const ResponseType*>(pre_serialized_response_sample->data);

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

    // Taking a shortcut here and skip the serialization/deserialization of messages and pretend
    // that the in memory data is already serialized.
    std::optional<EchoRequestPreSerializedSkeleton> request_skeleton_;
    std::optional<EchoResponsePreSerializedProxy> response_proxy_;

    std::mutex pending_mutex_;
    std::condition_variable response_cv_;
    std::unordered_map<std::uint64_t, PendingResponse> pending_responses_;
};

class IpcBenchmark : public benchmark::Fixture {
   public:
    void SetUp(const ::benchmark::State& /*state*/) override {
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

namespace {
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

// Helper function to calculate percentiles
double Percentile(const std::vector<double>& v, double percentile) {
    std::vector<double> sorted = v;
    std::sort(sorted.begin(), sorted.end());

    if (sorted.empty()) {
        return 0.0;
    }

    // Linear interpolation method
    double index = (percentile / 100.0) * (sorted.size() - 1);
    auto lower = static_cast<size_t>(std::floor(index));
    auto upper = static_cast<size_t>(std::ceil(index));

    if (lower == upper) {
        return sorted[lower];
    }

    double weight = index - lower;
    return (sorted[lower] * (1.0 - weight)) + (sorted[upper] * weight);
}
}  // namespace

// Latency benchmarks - measure round-trip time
BENCHMARK_DEFINE_F(IpcBenchmark, LatencyEcho)(benchmark::State& state) {
    auto payload_size = GetPayloadSizeFromArg(state.range(0));

    for (auto const& _ : state) {
        auto latency = BenchmarkFixture::Instance().SendEchoRequestSync(payload_size);
        if (latency.count() == 0) {
            state.SkipWithError("Failed to receive response or timeout occurred");
            break;
        }
        state.SetIterationTime(std::chrono::duration_cast<std::chrono::duration<double>>(latency)
                                   .count());  // Convert nanoseconds to seconds
    }

    state.SetLabel(GetPayloadSizeName(payload_size));
    state.counters["payload_bytes"] =
        benchmark::Counter(static_cast<double>(static_cast<std::uint32_t>(payload_size)),
                           benchmark::Counter::kIsIterationInvariant);
}

BENCHMARK_REGISTER_F(IpcBenchmark, LatencyEcho)
    ->Arg(0)  // Tiny
    // ->Arg(1)  // Small
    // ->Arg(2)  // Medium
    // ->Arg(3)  // Large
    // ->Arg(4)  // XLarge
    // ->Arg(5)  // XXLarge
    ->UseManualTime()
    ->Unit(benchmark::kMicrosecond)
    ->Repetitions(30)
    ->ComputeStatistics("p50", [](const std::vector<double>& v) { return Percentile(v, 50.0); })
    ->ComputeStatistics("p90", [](const std::vector<double>& v) { return Percentile(v, 90.0); })
    ->ComputeStatistics("p99", [](const std::vector<double>& v) { return Percentile(v, 99.0); });

// Throughput benchmarks - measure message sending rate
BENCHMARK_DEFINE_F(IpcBenchmark, ThroughputEcho)(benchmark::State& state) {
    auto payload_size = GetPayloadSizeFromArg(state.range(0));
    auto payload_bytes = static_cast<std::uint32_t>(payload_size);

    for (auto const& _ : state) {
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
    // ->Arg(1)  // Small
    // ->Arg(2)  // Medium
    // ->Arg(3)  // Large
    // ->Arg(4)  // XLarge
    // ->Arg(5)  // XXLarge
    ->Unit(benchmark::kMicrosecond);

// Stress test - send messages in batches to test system under high load
BENCHMARK_DEFINE_F(IpcBenchmark, StressThroughput)(benchmark::State& state) {
    auto payload_size = GetPayloadSizeFromArg(state.range(0));
    auto payload_bytes = static_cast<std::uint32_t>(payload_size);

    for (auto const& _ : state) {
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
    // ->Arg(1)  // Small
    // ->Arg(2)  // Medium
    // ->Arg(3)  // Large
    ->Unit(benchmark::kMicrosecond);

int main(int argc, char** argv) {
    std::signal(SIGINT, SigTermHandlerFunction);
    std::signal(SIGTERM, SigTermHandlerFunction);

    g_stop_source = score::cpp::stop_source{};
    g_stop_token = g_stop_source.get_token();

    std::string manifest_path;
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view{argv[index]} == "--service_instance_manifest") {
            manifest_path = argv[index + 1];
            break;
        }
    }
    if (manifest_path.empty()) {
        std::cerr << "Missing --service_instance_manifest" << std::endl;
        return 1;
    }
    score::mw::com::runtime::InitializeRuntime(
        score::mw::com::runtime::RuntimeConfiguration{score::filesystem::Path{manifest_path}});

    std::vector<char*> benchmark_args;
    benchmark_args.reserve(static_cast<std::size_t>(argc));
    benchmark_args.push_back(argv[0]);
    for (int index = 1; index < argc; ++index) {
        if (std::string_view{argv[index]} == "--service_instance_manifest" && index + 1 < argc) {
            ++index;
            continue;
        }
        benchmark_args.push_back(argv[index]);
    }
    auto benchmark_argc = static_cast<int>(benchmark_args.size());
    benchmark_args.push_back(nullptr);  // Ensure null-terminated for benchmark library
    benchmark::Initialize(&benchmark_argc, benchmark_args.data());

    if (benchmark::ReportUnrecognizedArguments(benchmark_argc, benchmark_args.data())) {
        return 1;
    }

    std::cout << "Starting IPC Performance Benchmarks..." << std::endl;
    std::cout << "Waiting for the echo server to become available..." << std::endl;

#if defined(__aarch64__) || defined(__arm64__)
    benchmark::AddCustomContext("architecture", "aarch64");
#elif defined(__x86_64__) || defined(_M_X64)
    benchmark::AddCustomContext("architecture", "x86_64");
#else
    benchmark::AddCustomContext("architecture", "unknown");
#endif

    if (g_stop_token.stop_requested()) {
        std::cout << "Stop requested before running benchmarks. Exiting..." << std::endl;
        return 0;
    }

    ErrorTrackingReporter reporter;
    benchmark::RunSpecifiedBenchmarks(&reporter);

    BenchmarkFixture::Instance().Cleanup();

    return reporter.HadError() ? EXIT_FAILURE : EXIT_SUCCESS;
}
