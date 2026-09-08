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
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include "echo_service.h"
#include "score/mw/com/runtime.h"
#include "score/someip/constants.h"
#include "score/stop_token.hpp"

using namespace echo_service;
using namespace std::chrono_literals;

constexpr std::uint16_t MaxSamplesCount{score::someip::kMaxSampleCount};
constexpr std::uint8_t MAX_SERVICE_DISCOVERY_RETRIES{30};
constexpr auto SERVICE_DISCOVERY_RETRY_INTERVAL{1s};
constexpr auto SEQUENTIAL_HANDSHAKE_DELAY{2s};
constexpr auto RESPONSE_TIMEOUT{1s};

constexpr const char* EchoRequestkInstanceSpecifier = "benchmark/echo_request";
constexpr const char* EchoResponseInstanceSpecifier = "benchmark/echo_response";

struct PayloadConfig {
    PayloadSize size;
    const char* name;
};

constexpr std::array<PayloadConfig, 6> PAYLOAD_CONFIGS = {{{PayloadSize::Tiny, "Tiny_8B"},
                                                           {PayloadSize::Small, "Small_64B"},
                                                           {PayloadSize::Medium, "Medium_1KB"},
                                                           {PayloadSize::Large, "Large_8KB"},
                                                           {PayloadSize::XLarge, "XLarge_64KB"},
                                                           {PayloadSize::XXLarge, "XXLarge_1MB"}}};

constexpr size_t NUM_PAYLOAD_CONFIGS = PAYLOAD_CONFIGS.size();

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

class Event_wrapper {
   public:
    using time_point = std::chrono::high_resolution_clock::time_point;

    Event_wrapper(
        std::function<void()> subscribe, std::function<void()> set_receive_handler,
        std::function<SequenceId()> send_async,
        std::function<std::chrono::nanoseconds(SequenceId, time_point)> receive_sync_with_polling,
        std::function<void()> unset_receive_handler, std::function<void()> unsubscribe)
        : subscribe_{std::move(subscribe)},
          set_receive_handler_{std::move(set_receive_handler)},
          send_async_{std::move(send_async)},
          receive_sync_with_polling_{std::move(receive_sync_with_polling)},
          unset_receive_handler_{std::move(unset_receive_handler)},
          unsubscribe_{std::move(unsubscribe)} {}

    ~Event_wrapper() {
        try {
            Unsubscribe();
            UnsetReceiveHandler();
        } catch (const std::exception& e) {
            std::cerr << "Exception in Event_wrapper destructor: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception in Event_wrapper destructor" << std::endl;
        }
    }

    void Subscribe() { subscribe_(); }

    void SetReceiveHandler() { set_receive_handler_(); }

    SequenceId SendAsync() { return send_async_(); }

    std::chrono::nanoseconds ReceiveSyncWithPolling(SequenceId sequence_id, time_point send_time) {
        return receive_sync_with_polling_(sequence_id, send_time);
    }

    void UnsetReceiveHandler() { unset_receive_handler_(); }

    void Unsubscribe() { unsubscribe_(); }

   private:
    std::function<void()> subscribe_;
    std::function<void()> set_receive_handler_;
    std::function<SequenceId()> send_async_;
    std::function<std::chrono::nanoseconds(SequenceId, time_point)> receive_sync_with_polling_;
    std::function<void()> unset_receive_handler_;
    std::function<void()> unsubscribe_;
};

class BenchmarkFixture {
   public:
    static BenchmarkFixture& Instance() {
        static BenchmarkFixture instance;
        return instance;
    }

    void ResetCounters() {
        next_sequence_id_ = 1;
        num_lost_sequence_ids = 0;
        pending_responses_.clear();
    }

    void Initialize() {
        ResetCounters();

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

        initialized_ = true;
        std::cout << "Benchmark infrastructure initialized successfully - ready to start benchmarks"
                  << std::endl;
    }

    void Cleanup() {
        if (!initialized_) {
            return;
        }

        response_proxy_.reset();
        request_skeleton_.reset();
        initialized_ = false;
        std::cout << "Benchmark infrastructure cleaned up" << std::endl;
    }

    std::size_t get_num_lost_sequence_ids() const { return num_lost_sequence_ids.load(); }

    SequenceId get_num_in_flight_messages() const {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        return pending_responses_.size();
    }

    Event_wrapper GetEventWrapper(PayloadSize size) {
        switch (size) {
            case PayloadSize::Tiny:
                return MakeEventWrapper<EchoRequestTiny, EchoResponseTiny>(
                    request_skeleton_->echo_request_tiny_, response_proxy_->echo_response_tiny_,
                    size);
            case PayloadSize::Small:
                return MakeEventWrapper<EchoRequestSmall, EchoResponseSmall>(
                    request_skeleton_->echo_request_small_, response_proxy_->echo_response_small_,
                    size);
            case PayloadSize::Medium:
                return MakeEventWrapper<EchoRequestMedium, EchoResponseMedium>(
                    request_skeleton_->echo_request_medium_, response_proxy_->echo_response_medium_,
                    size);
            case PayloadSize::Large:
                return MakeEventWrapper<EchoRequestLarge, EchoResponseLarge>(
                    request_skeleton_->echo_request_large_, response_proxy_->echo_response_large_,
                    size);
            case PayloadSize::XLarge:
                return MakeEventWrapper<EchoRequestXLarge, EchoResponseXLarge>(
                    request_skeleton_->echo_request_xlarge_, response_proxy_->echo_response_xlarge_,
                    size);
            case PayloadSize::XXLarge:
                return MakeEventWrapper<EchoRequestXXLarge, EchoResponseXXLarge>(
                    request_skeleton_->echo_request_xxlarge_,
                    response_proxy_->echo_response_xxlarge_, size);
        }
        throw std::runtime_error("Unsupported payload size");
    }

   private:
    template <typename ResponseType, typename EventType>
    std::chrono::nanoseconds ReceiveEchoRequestSyncWithPolling(
        EventType& response_event, SequenceId sequence_id,
        std::chrono::high_resolution_clock::time_point send_time) {
        auto start_time = std::chrono::high_resolution_clock::now();

        while (std::chrono::high_resolution_clock::now() - start_time < RESPONSE_TIMEOUT) {
            if (g_stop_token.stop_requested()) {
                std::cout << "Stop requested during polling for sequence_id: " << sequence_id
                          << std::endl;
                return std::chrono::nanoseconds{0};
            }

            bool found = false;
            std::chrono::high_resolution_clock::time_point receive_time;

            (void)response_event.GetNewSamples(
                [&](auto pre_serialized_response_sample) {
                    static_assert(
                        sizeof(ResponseType) <=
                            decltype(pre_serialized_response_sample)::element_type::kMaxMessageSize,
                        "Echo response size exceeds max sample count");
                    SCORE_LANGUAGE_FUTURECPP_ASSERT(pre_serialized_response_sample->size ==
                                                    sizeof(ResponseType));
                    const auto* response_sample =
                        reinterpret_cast<const ResponseType*>(pre_serialized_response_sample->data);

                    if (response_sample->sequence_id == sequence_id) {
                        receive_time = std::chrono::high_resolution_clock::now();
                        found = true;
                    }
                },
                MaxSamplesCount);

            if (found) {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                pending_responses_.erase(sequence_id);
                return std::chrono::duration_cast<std::chrono::nanoseconds>(receive_time -
                                                                            send_time);
            }

            // Small delay to avoid busy waiting
            std::this_thread::yield();
        }

        throw std::runtime_error(
            "Timeout waiting for echo response. Sequence ID: " + std::to_string(sequence_id) +
            ". Check if echo_server is properly handling requests.");
    }

    template <typename RequestType, typename ResponseType, typename RequestEventType,
              typename ResponseEventType>
    Event_wrapper MakeEventWrapper(RequestEventType& request_event,
                                   ResponseEventType& response_event, PayloadSize size) {
        return Event_wrapper{
            [&response_event, size]() {
                std::cout << "Subscribing to echo_response service event" << static_cast<int>(size)
                          << "..." << std::endl;
                (void)response_event.Subscribe(MaxSamplesCount);

                std::cout << "Waiting for echo server to connect..." << std::endl;
                std::this_thread::sleep_for(SEQUENTIAL_HANDSHAKE_DELAY);
            },
            [this, &response_event]() {
                auto handler_result = response_event.SetReceiveHandler([this, &response_event]() {
                    this->ProcessResponsesThroughput<ResponseType>(response_event);
                });
                if (!handler_result.has_value()) {
                    throw std::runtime_error("Failed to set response handler");
                }
            },
            [this, &request_event, size]() {
                auto actual_size = static_cast<std::uint32_t>(size);
                auto sequence_id = next_sequence_id_++;
                {
                    std::unique_lock<std::mutex> lock(pending_mutex_);
                    pending_responses_.insert(sequence_id);
                }

                SendRequest<RequestType>(request_event, size, sequence_id, actual_size);
                return sequence_id;
            },
            [this, &response_event](SequenceId sequence_id, Event_wrapper::time_point send_time) {
                return ReceiveEchoRequestSyncWithPolling<ResponseType>(response_event, sequence_id,
                                                                       send_time);
            },
            [&response_event]() { (void)response_event.UnsetReceiveHandler(); },
            [&response_event]() { response_event.Unsubscribe(); }};
    }

    template <typename RequestType, typename EventType>
    void SendRequest(EventType& request_event, PayloadSize size, SequenceId sequence_id,
                     std::uint32_t actual_size) {
        auto timeout = std::chrono::steady_clock::now() + RESPONSE_TIMEOUT;

        auto pre_serialized_request_result = request_event.Allocate();
        while (!pre_serialized_request_result.has_value()) {
            if (timeout < std::chrono::steady_clock::now()) {
                throw std::runtime_error(
                    "Timeout waiting for available slot to send echo request. Sequence ID: " +
                    std::to_string(sequence_id));
            }

            if (g_stop_token.stop_requested()) {
                return;
            }
            std::this_thread::yield();
            pre_serialized_request_result = request_event.Allocate();
        }

        auto pre_serialized_request = std::move(pre_serialized_request_result.value());
        pre_serialized_request->size = sizeof(RequestType);
        auto* request = reinterpret_cast<RequestType*>(pre_serialized_request->data);
        request->sequence_id = sequence_id;
        request->timestamp_ns = utils::GetCurrentTimeNanos();
        request->payload_size = size;
        request->actual_size = actual_size;
        utils::FillTestPayload(request->payload, actual_size, sequence_id);
        (void)request_event.Send(std::move(pre_serialized_request));
    }

    template <typename ResponseType, typename EventType>
    void ProcessResponsesThroughput(EventType& response_event) {
        if (g_stop_token.stop_requested()) {
            return;
        }

        std::vector<SequenceId> received_sequence_ids;
        received_sequence_ids.reserve(MaxSamplesCount);

        (void)response_event.GetNewSamples(
            [&received_sequence_ids](auto pre_serialized_response_sample) {
                SCORE_LANGUAGE_FUTURECPP_ASSERT(pre_serialized_response_sample->size ==
                                                sizeof(ResponseType));
                auto* response_sample =
                    reinterpret_cast<const ResponseType*>(pre_serialized_response_sample->data);

                received_sequence_ids.push_back(response_sample->sequence_id);
            },
            MaxSamplesCount);

        if (received_sequence_ids.empty()) {
            return;
        }

        std::lock_guard<std::mutex> lock(pending_mutex_);

        for (auto const& sequence_id : received_sequence_ids) {
            pending_responses_.erase(sequence_id);
        }

        // In the system there are currently at least 4 buffers of size kMaxSampleCount, thus in
        // flight messages might be able to fill those
        constexpr SequenceId kMaxInFlightMessages = score::someip::kMaxSampleCount * 5U;
        const auto next_sequence_id = next_sequence_id_.load();
        const auto min_sequence_id = next_sequence_id > kMaxInFlightMessages
                                         ? next_sequence_id - kMaxInFlightMessages
                                         : SequenceId{1};
        const auto current_pending_size = pending_responses_.size();
        for (auto it = pending_responses_.begin(); it != pending_responses_.end();) {
            if (*it < min_sequence_id) {
                it = pending_responses_.erase(it);
            } else {
                ++it;
            }
        }
        num_lost_sequence_ids += current_pending_size - pending_responses_.size();
    }

    bool initialized_{false};
    std::atomic<SequenceId> next_sequence_id_{1};
    std::atomic<std::size_t> num_lost_sequence_ids{0};

    // Taking a shortcut here and skip the serialization/deserialization of messages and pretend
    // that the in memory data is already serialized.
    std::optional<EchoRequestPreSerializedSkeleton> request_skeleton_;
    std::optional<EchoResponsePreSerializedProxy> response_proxy_;

    mutable std::mutex pending_mutex_;
    std::unordered_set<SequenceId> pending_responses_;
};

class IpcBenchmark : public benchmark::Fixture {
   public:
    void SetUp(const ::benchmark::State& /*state*/) override {
        BenchmarkFixture::Instance().Initialize();
    }

    void TearDown(const ::benchmark::State& /*state*/) override {
        // Cleanup is done in global teardown
    }
};

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
    auto event_wrapper = BenchmarkFixture::Instance().GetEventWrapper(payload_size);
    event_wrapper.Subscribe();

    for (auto const& _ : state) {
        auto send_time = std::chrono::high_resolution_clock::now();
        auto sequence_id = event_wrapper.SendAsync();
        auto latency = event_wrapper.ReceiveSyncWithPolling(sequence_id, send_time);

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
    ->UseManualTime()
    ->Unit(benchmark::kMicrosecond)
    ->Repetitions(30)
    ->ComputeStatistics("p50", [](const std::vector<double>& v) { return Percentile(v, 50.0); })
    ->ComputeStatistics("p90", [](const std::vector<double>& v) { return Percentile(v, 90.0); })
    ->ComputeStatistics("p99", [](const std::vector<double>& v) { return Percentile(v, 99.0); });

// Throughput benchmarks - measure the rate of messages echoed back by the echo server
// All shared memory slots have the same number of buffers and we block until a new mw::com buffer
// is available. This eventually leads to a self regulating system with some but minimal message
// loss.
// At first a more complex algorithm was implemented to track the number of in-flight messages and
// drop messages when the number of in-flight messages exceeded a threshold. However, this basically
// achieved the same values we have now with much more complexity.
BENCHMARK_DEFINE_F(IpcBenchmark, Throughput)(benchmark::State& state) {
    auto payload_size = GetPayloadSizeFromArg(state.range(0));
    auto payload_bytes = static_cast<std::uint32_t>(payload_size);
    auto event_wrapper = BenchmarkFixture::Instance().GetEventWrapper(payload_size);
    event_wrapper.SetReceiveHandler();
    event_wrapper.Subscribe();

    auto& fixture = BenchmarkFixture::Instance();

    for (auto const& _ : state) {
        // blocks when buffers are full
        event_wrapper.SendAsync();
    }

    auto const sent_messages = state.iterations();
    auto const dropped_messages = fixture.get_num_lost_sequence_ids();
    auto const received_messages =
        sent_messages - dropped_messages - fixture.get_num_in_flight_messages();

    state.SetLabel(GetPayloadSizeName(payload_size));
    state.counters["payload_bytes"] = static_cast<double>(payload_bytes);
    state.counters["sent_messages"] = static_cast<double>(sent_messages);
    state.counters["received_messages"] = static_cast<double>(received_messages);
    state.counters["dropped_messages"] = static_cast<double>(dropped_messages);
    state.counters["drop_ratio"] = sent_messages > 0 ? static_cast<double>(dropped_messages) /
                                                           static_cast<double>(sent_messages)
                                                     : 0.0;
    state.counters["bytes_per_sec"] = benchmark::Counter(
        static_cast<double>(received_messages * payload_bytes), benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(IpcBenchmark, Throughput)
    ->Arg(0)  // Tiny
    ->Arg(1)  // Small
    ->Arg(2)  // Medium
    ->Arg(3)  // Large
    ->Arg(4)  // XLarge
    ->Arg(5)  // XXLarge
    ->MinWarmUpTime(1)
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
