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

/// @file
/// Consumes the mw::com events forwarded by gatewayd B and reports one-way latency, throughput and
/// loss. In roundtrip mode every message is echoed back towards the sender.

#include <getopt.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "perf_service.h"
#include "score/mw/com/runtime.h"
#include "stats.h"

namespace {

using namespace perf_service;
using namespace std::chrono_literals;

constexpr const char* kRequestInstanceSpecifier = "perf/request_rx";
constexpr const char* kResponseInstanceSpecifier = "perf/response";
constexpr std::size_t kMaxSampleCount{10};
constexpr auto kDiscoveryRetryInterval{200ms};
constexpr auto kPollInterval{50us};

struct Options {
    std::string manifest{};
    PerfPayloadSize payload_size{PerfPayloadSize::Tiny};
    std::uint64_t count{10000};
    std::uint64_t warmup{100};
    bool roundtrip{false};
    double timeout_s{60.0};
    double idle_timeout_s{5.0};
    std::uint32_t run_id{1};
    std::string result_json{};
    std::string ready_file{};
};

void PrintHelp() {
    std::cout
        << "Syntax: perf_receiver -s/--service_instance_manifest <manifest.json> [options]\n\n"
        << "Options:\n"
        << " -s/--service_instance_manifest mw::com manifest of this node (required)\n"
        << " -p/--payload_size              tiny|small|medium (default: tiny)\n"
        << " -n/--count                     Number of measured messages to expect (default: "
           "10000)\n"
        << " -w/--warmup                    Sequence ids below this are discarded (default: 100)\n"
        << " -m/--mode                      oneway|roundtrip (default: oneway)\n"
        << " -t/--timeout_s                 Overall timeout in seconds (default: 60)\n"
        << " -u/--idle_timeout_s            Stop this long after the last message (default: 5)\n"
        << " -I/--run_id                    Only accept messages stamped with this run id\n"
        << " -o/--result_json               File to write the result JSON to\n"
        << " -R/--ready_file                File created once the receiver is subscribed\n"
        << " -h/--help                      Displays this help\n\n";
}

bool ParseOptions(int argc, char* argv[], Options& options) {
    const char* const short_opts = "hs:p:n:w:m:t:u:I:o:R:";
    const option long_opts[] = {{"help", no_argument, nullptr, 'h'},
                                {"service_instance_manifest", required_argument, nullptr, 's'},
                                {"payload_size", required_argument, nullptr, 'p'},
                                {"count", required_argument, nullptr, 'n'},
                                {"warmup", required_argument, nullptr, 'w'},
                                {"mode", required_argument, nullptr, 'm'},
                                {"timeout_s", required_argument, nullptr, 't'},
                                {"idle_timeout_s", required_argument, nullptr, 'u'},
                                {"run_id", required_argument, nullptr, 'I'},
                                {"result_json", required_argument, nullptr, 'o'},
                                {"ready_file", required_argument, nullptr, 'R'},
                                {nullptr, no_argument, nullptr, 0}};

    while (true) {
        const int opt{getopt_long(argc, argv, short_opts, long_opts, nullptr)};
        if (opt == -1) {
            break;
        }
        switch (static_cast<char>(opt)) {
            case 's':
                options.manifest = optarg;
                break;
            case 'p':
                if (!ParsePayloadSize(optarg, options.payload_size)) {
                    std::cerr << "Unknown payload size: " << optarg << std::endl;
                    return false;
                }
                break;
            case 'n':
                options.count = std::strtoull(optarg, nullptr, 10);
                break;
            case 'w':
                options.warmup = std::strtoull(optarg, nullptr, 10);
                break;
            case 'm':
                options.roundtrip = (std::string{optarg} == "roundtrip");
                break;
            case 't':
                options.timeout_s = std::strtod(optarg, nullptr);
                break;
            case 'u':
                options.idle_timeout_s = std::strtod(optarg, nullptr);
                break;
            case 'I':
                options.run_id = static_cast<std::uint32_t>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'o':
                options.result_json = optarg;
                break;
            case 'R':
                options.ready_file = optarg;
                break;
            case 'h':
            default:
                PrintHelp();
                return false;
        }
    }
    return !options.manifest.empty();
}

std::optional<PerfRequestProxy> FindRequestProxy(std::chrono::steady_clock::time_point deadline) {
    while (std::chrono::steady_clock::now() < deadline) {
        auto handles = PerfRequestProxy::FindService(
            score::mw::com::InstanceSpecifier::Create(std::string{kRequestInstanceSpecifier})
                .value());
        if (handles.has_value() && !handles.value().empty()) {
            auto proxy = PerfRequestProxy::Create(handles.value().front());
            if (proxy.has_value()) {
                return std::move(proxy).value();
            }
        }
        std::this_thread::sleep_for(kDiscoveryRetryInterval);
    }
    return std::nullopt;
}

struct Measurement {
    std::vector<std::uint64_t> latencies_ns{};
    std::uint64_t received{0};
    std::uint64_t corrupt{0};
    std::uint64_t stale{0};
    std::uint64_t discarded_warmup{0};
    std::uint64_t lowest_sequence_id{0};
    std::uint64_t highest_sequence_id{0};
    std::uint64_t first_receive_ns{0};
    std::uint64_t last_receive_ns{0};
};

template <PerfPayloadSize PayloadBytes>
void EchoMessage(PerfResponseSkeleton& skeleton, const PerfMessage<PayloadBytes>& message) {
    auto& event = EventSelector<PayloadBytes>::Response(skeleton);
    auto allocated = event.Allocate();
    if (!allocated.has_value()) {
        return;
    }
    auto sample = std::move(allocated).value();
    WriteMessage<PayloadBytes>(*sample, message);
    std::ignore = event.Send(std::move(sample));
}

template <PerfPayloadSize PayloadBytes>
int RunReceiver(const Options& options) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::duration<double>(options.timeout_s));

    auto proxy = FindRequestProxy(deadline);
    if (!proxy.has_value()) {
        std::cerr << "Request service not found within timeout" << std::endl;
        return EXIT_FAILURE;
    }
    auto& request_event = EventSelector<PayloadBytes>::Request(*proxy);
    if (!request_event.Subscribe(kMaxSampleCount).has_value()) {
        std::cerr << "Failed to subscribe to request event" << std::endl;
        return EXIT_FAILURE;
    }

    std::optional<PerfResponseSkeleton> response_skeleton{};
    if (options.roundtrip) {
        auto skeleton_result = PerfResponseSkeleton::Create(
            score::mw::com::InstanceSpecifier::Create(std::string{kResponseInstanceSpecifier})
                .value());
        if (!skeleton_result.has_value()) {
            std::cerr << "Failed to create response skeleton" << std::endl;
            return EXIT_FAILURE;
        }
        response_skeleton = std::move(skeleton_result).value();
        if (!response_skeleton->OfferService().has_value()) {
            std::cerr << "Failed to offer response service" << std::endl;
            return EXIT_FAILURE;
        }
    }

    if (!options.ready_file.empty()) {
        std::ofstream{options.ready_file} << "ready\n";
    }

    Measurement measurement{};
    measurement.lowest_sequence_id = options.warmup + options.count;
    const auto idle_timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(options.idle_timeout_s));
    auto last_activity = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() < deadline && measurement.received < options.count) {
        bool progressed = false;
        request_event.GetNewSamples(
            [&](auto sample) {
                PerfMessage<PayloadBytes> message{};
                if (!ReadMessage<PayloadBytes>(*sample, message)) {
                    ++measurement.corrupt;
                    return;
                }
                if (message.run_id != options.run_id) {
                    // Left over in the buffers from an earlier run.
                    ++measurement.stale;
                    return;
                }
                if (!VerifyTestPayload(message.payload, message.payload_bytes)) {
                    ++measurement.corrupt;
                    return;
                }
                progressed = true;
                if (response_skeleton.has_value()) {
                    EchoMessage<PayloadBytes>(*response_skeleton, message);
                }
                if (message.sequence_id < options.warmup) {
                    ++measurement.discarded_warmup;
                    return;
                }

                const auto now = GetCurrentTimeNanos();
                measurement.latencies_ns.push_back(now - message.send_timestamp_ns);
                if (measurement.received == 0) {
                    measurement.first_receive_ns = now;
                }
                measurement.last_receive_ns = now;
                measurement.lowest_sequence_id =
                    std::min(measurement.lowest_sequence_id, message.sequence_id);
                measurement.highest_sequence_id =
                    std::max(measurement.highest_sequence_id, message.sequence_id);
                ++measurement.received;
            },
            kMaxSampleCount);

        const auto now = std::chrono::steady_clock::now();
        if (progressed) {
            last_activity = now;
        } else if (measurement.received > 0 && now - last_activity > idle_timeout) {
            break;
        }
        std::this_thread::sleep_for(kPollInterval);
    }

    request_event.Unsubscribe();

    const double duration_s =
        measurement.received > 1
            ? static_cast<double>(measurement.last_receive_ns - measurement.first_receive_ns) / 1e9
            : 0.0;
    const auto stats = ComputeLatencyStats(measurement.latencies_ns);
    // Anything the sender emitted between the first and last received sequence id but that never
    // arrived counts as lost.
    const std::uint64_t expected_span =
        measurement.received > 0
            ? measurement.highest_sequence_id - measurement.lowest_sequence_id + 1
            : 0;
    const std::uint64_t lost =
        expected_span > measurement.received ? expected_span - measurement.received : 0;

    std::ostringstream json{};
    json << "{\n  \"role\": \"receiver\",\n"
         << "  \"mode\": \"" << (options.roundtrip ? "roundtrip" : "oneway") << "\",\n"
         << "  \"payload_size\": \"" << PayloadSizeName(PayloadBytes) << "\",\n"
         << "  \"payload_bytes\": " << static_cast<std::uint32_t>(PayloadBytes) << ",\n"
         << "  \"message_bytes\": " << sizeof(PerfMessage<PayloadBytes>) << ",\n"
         << "  \"run_id\": " << options.run_id << ",\n"
         << "  \"expected\": " << options.count << ",\n"
         << "  \"received\": " << measurement.received << ",\n"
         << "  \"lost\": " << lost << ",\n"
         << "  \"corrupt\": " << measurement.corrupt << ",\n"
         << "  \"stale\": " << measurement.stale << ",\n"
         << "  \"discarded_warmup\": " << measurement.discarded_warmup << ",\n"
         << "  \"duration_s\": " << duration_s << ",\n"
         << "  \"throughput_msgs_per_s\": "
         << (duration_s > 0.0 ? static_cast<double>(measurement.received) / duration_s : 0.0)
         << ",\n"
         << "  \"throughput_mb_per_s\": "
         << (duration_s > 0.0 ? static_cast<double>(measurement.received) *
                                    static_cast<double>(sizeof(PerfMessage<PayloadBytes>)) /
                                    duration_s / (1024.0 * 1024.0)
                              : 0.0)
         << ",\n"
         << "  \"oneway_latency\": ";
    WriteLatencyStatsJson(json, stats);
    json << "\n}\n";

    std::cout << json.str();
    if (!options.result_json.empty()) {
        std::ofstream out{options.result_json};
        if (!out) {
            std::cerr << "Failed to write " << options.result_json << std::endl;
            return EXIT_FAILURE;
        }
        out << json.str();
    }

    return measurement.received > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int main(int argc, char* argv[]) {
    Options options{};
    if (!ParseOptions(argc, argv, options)) {
        PrintHelp();
        return EXIT_FAILURE;
    }

    score::mw::com::runtime::InitializeRuntime(
        score::mw::com::runtime::RuntimeConfiguration{score::filesystem::Path{options.manifest}});

    switch (options.payload_size) {
        case PerfPayloadSize::Tiny:
            return RunReceiver<PerfPayloadSize::Tiny>(options);
        case PerfPayloadSize::Small:
            return RunReceiver<PerfPayloadSize::Small>(options);
        case PerfPayloadSize::Medium:
            return RunReceiver<PerfPayloadSize::Medium>(options);
    }
    return EXIT_FAILURE;
}
