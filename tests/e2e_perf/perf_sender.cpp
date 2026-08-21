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
/// Publishes mw::com events that travel through gatewayd A -> someipd A -> SOME/IP -> someipd B ->
/// gatewayd B to the perf_receiver. In roundtrip mode it also consumes the echoed response and
/// reports the round-trip latency.

#include <getopt.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
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

constexpr const char* kRequestInstanceSpecifier = "perf/request";
constexpr const char* kResponseInstanceSpecifier = "perf/response_rx";
constexpr auto kDiscoveryRetryInterval{200ms};
constexpr auto kWarmupInterval{20ms};

struct Options {
    std::string manifest{};
    PerfPayloadSize payload_size{PerfPayloadSize::Tiny};
    std::uint64_t count{10000};
    double rate_hz{0.0};
    std::uint64_t warmup{100};
    bool roundtrip{false};
    double timeout_s{60.0};
    std::uint32_t run_id{1};
    std::string result_json{};
};

void PrintHelp() {
    std::cout
        << "Syntax: perf_sender -s/--service_instance_manifest <manifest.json> [options]\n\n"
        << "Options:\n"
        << " -s/--service_instance_manifest mw::com manifest of this node (required)\n"
        << " -p/--payload_size              tiny|small|medium|large|xlarge|xxlarge (default: "
           "tiny)\n"
        << " -n/--count                     Number of measured messages (default: 10000)\n"
        << " -r/--rate_hz                   Send rate, 0 means as fast as possible (default: 0)\n"
        << " -w/--warmup                    Number of warmup messages (default: 100)\n"
        << " -m/--mode                      oneway|roundtrip (default: oneway)\n"
        << " -t/--timeout_s                 Overall timeout in seconds (default: 60)\n"
        << " -I/--run_id                    Run identifier stamped into every message\n"
        << " -o/--result_json               File to write the result JSON to\n"
        << " -h/--help                      Displays this help\n\n";
}

bool ParseOptions(int argc, char* argv[], Options& options) {
    const char* const short_opts = "hs:p:n:r:w:m:t:I:o:";
    const option long_opts[] = {{"help", no_argument, nullptr, 'h'},
                                {"service_instance_manifest", required_argument, nullptr, 's'},
                                {"payload_size", required_argument, nullptr, 'p'},
                                {"count", required_argument, nullptr, 'n'},
                                {"rate_hz", required_argument, nullptr, 'r'},
                                {"warmup", required_argument, nullptr, 'w'},
                                {"mode", required_argument, nullptr, 'm'},
                                {"timeout_s", required_argument, nullptr, 't'},
                                {"run_id", required_argument, nullptr, 'I'},
                                {"result_json", required_argument, nullptr, 'o'},
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
            case 'r':
                options.rate_hz = std::strtod(optarg, nullptr);
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
            case 'I':
                options.run_id = static_cast<std::uint32_t>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'o':
                options.result_json = optarg;
                break;
            case 'h':
            default:
                PrintHelp();
                return false;
        }
    }
    return !options.manifest.empty();
}

std::optional<PerfResponseProxy> FindResponseProxy(std::chrono::steady_clock::time_point deadline) {
    while (std::chrono::steady_clock::now() < deadline) {
        auto handles = PerfResponseProxy::FindService(
            score::mw::com::InstanceSpecifier::Create(std::string{kResponseInstanceSpecifier})
                .value());
        if (handles.has_value() && !handles.value().empty()) {
            auto proxy = PerfResponseProxy::Create(handles.value().front());
            if (proxy.has_value()) {
                return std::move(proxy).value();
            }
        }
        std::this_thread::sleep_for(kDiscoveryRetryInterval);
    }
    return std::nullopt;
}

/// Drains the response event and turns every echoed message into a round-trip latency sample.
template <PerfPayloadSize PayloadBytes>
void DrainResponses(PerfResponseProxy& proxy, std::uint32_t run_id,
                    std::vector<std::uint64_t>& rtt_samples) {
    auto& event = EventSelector<PayloadBytes>::Response(proxy);
    event.GetNewSamples(
        [run_id, &rtt_samples](auto sample) {
            auto message = std::make_unique<PerfMessage<PayloadBytes>>();
            if (!ReadMessage<PayloadBytes>(*sample, *message) || message->run_id != run_id) {
                return;
            }
            // The receiver echoes the original timestamp, so no per-sequence bookkeeping is needed.
            rtt_samples.push_back(GetCurrentTimeNanos() - message->send_timestamp_ns);
        },
        SampleSlotCount<PayloadBytes>());
}

template <PerfPayloadSize PayloadBytes>
bool SendOne(PerfRequestSkeleton& skeleton, std::uint32_t run_id, std::uint64_t sequence_id) {
    auto& event = EventSelector<PayloadBytes>::Request(skeleton);
    auto allocated = event.Allocate();
    if (!allocated.has_value()) {
        return false;
    }
    auto sample = std::move(allocated).value();

    auto message = std::make_unique<PerfMessage<PayloadBytes>>();
    message->sequence_id = sequence_id;
    message->run_id = run_id;
    message->payload_bytes = static_cast<std::uint32_t>(PayloadBytes);
    FillTestPayload(message->payload, message->payload_bytes);
    message->send_timestamp_ns = GetCurrentTimeNanos();

    WriteMessage<PayloadBytes>(*sample, *message);
    return event.Send(std::move(sample)).has_value();
}

template <PerfPayloadSize PayloadBytes>
int RunSender(const Options& options) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::duration<double>(options.timeout_s));

    auto skeleton_result = PerfRequestSkeleton::Create(
        score::mw::com::InstanceSpecifier::Create(std::string{kRequestInstanceSpecifier}).value());
    if (!skeleton_result.has_value()) {
        std::cerr << "Failed to create request skeleton" << std::endl;
        return EXIT_FAILURE;
    }
    auto skeleton = std::move(skeleton_result).value();
    if (!skeleton.OfferService().has_value()) {
        std::cerr << "Failed to offer request service" << std::endl;
        return EXIT_FAILURE;
    }

    std::optional<PerfResponseProxy> response_proxy{};
    if (options.roundtrip) {
        response_proxy = FindResponseProxy(deadline);
        if (!response_proxy.has_value()) {
            std::cerr << "Response service not found within timeout" << std::endl;
            return EXIT_FAILURE;
        }
        EventSelector<PayloadBytes>::Response(*response_proxy)
            .Subscribe(SampleSlotCount<PayloadBytes>());
    }

    std::vector<std::uint64_t> rtt_samples{};
    rtt_samples.reserve(options.count);

    // Warmup traffic gives service discovery and the subscriptions along the chain time to settle;
    // the receiver discards everything below `warmup`.
    for (std::uint64_t sequence_id{0}; sequence_id < options.warmup; ++sequence_id) {
        SendOne<PayloadBytes>(skeleton, options.run_id, sequence_id);
        std::this_thread::sleep_for(kWarmupInterval);
        if (response_proxy.has_value()) {
            DrainResponses<PayloadBytes>(*response_proxy, options.run_id, rtt_samples);
        }
    }
    rtt_samples.clear();

    const auto send_interval = options.rate_hz > 0.0
                                   ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::duration<double>(1.0 / options.rate_hz))
                                   : std::chrono::nanoseconds::zero();

    std::uint64_t sent{0};
    std::uint64_t send_failures{0};
    const auto start = std::chrono::steady_clock::now();
    auto next_send = start;

    for (std::uint64_t i{0}; i < options.count; ++i) {
        if (send_interval.count() > 0) {
            std::this_thread::sleep_until(next_send);
            next_send += send_interval;
        }
        if (SendOne<PayloadBytes>(skeleton, options.run_id, options.warmup + i)) {
            ++sent;
        } else {
            ++send_failures;
        }
        if (response_proxy.has_value()) {
            DrainResponses<PayloadBytes>(*response_proxy, options.run_id, rtt_samples);
        }
    }
    const auto send_duration = std::chrono::steady_clock::now() - start;

    if (response_proxy.has_value()) {
        const auto drain_deadline = std::min(deadline, std::chrono::steady_clock::now() + 5s);
        while (std::chrono::steady_clock::now() < drain_deadline &&
               rtt_samples.size() < options.count) {
            DrainResponses<PayloadBytes>(*response_proxy, options.run_id, rtt_samples);
            std::this_thread::sleep_for(1ms);
        }
        EventSelector<PayloadBytes>::Response(*response_proxy).Unsubscribe();
    }

    const double send_duration_s = std::chrono::duration<double>(send_duration).count();
    const auto rtt_stats = ComputeLatencyStats(rtt_samples);

    std::ostringstream json{};
    json << "{\n  \"role\": \"sender\",\n"
         << "  \"mode\": \"" << (options.roundtrip ? "roundtrip" : "oneway") << "\",\n"
         << "  \"payload_size\": \"" << PayloadSizeName(PayloadBytes) << "\",\n"
         << "  \"payload_bytes\": " << static_cast<std::uint32_t>(PayloadBytes) << ",\n"
         << "  \"message_bytes\": " << sizeof(PerfMessage<PayloadBytes>) << ",\n"
         << "  \"run_id\": " << options.run_id << ",\n"
         << "  \"requested\": " << options.count << ",\n"
         << "  \"sent\": " << sent << ",\n"
         << "  \"send_failures\": " << send_failures << ",\n"
         << "  \"send_duration_s\": " << send_duration_s << ",\n"
         << "  \"achieved_rate_hz\": "
         << (send_duration_s > 0.0 ? static_cast<double>(sent) / send_duration_s : 0.0) << ",\n"
         << "  \"responses_received\": " << rtt_samples.size() << ",\n"
         << "  \"roundtrip_latency\": ";
    WriteLatencyStatsJson(json, rtt_stats);
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

    return send_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
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
            return RunSender<PerfPayloadSize::Tiny>(options);
        case PerfPayloadSize::Small:
            return RunSender<PerfPayloadSize::Small>(options);
        case PerfPayloadSize::Medium:
            return RunSender<PerfPayloadSize::Medium>(options);
        case PerfPayloadSize::Large:
            return RunSender<PerfPayloadSize::Large>(options);
        case PerfPayloadSize::XLarge:
            return RunSender<PerfPayloadSize::XLarge>(options);
        case PerfPayloadSize::XXLarge:
            return RunSender<PerfPayloadSize::XXLarge>(options);
    }
    return EXIT_FAILURE;
}
