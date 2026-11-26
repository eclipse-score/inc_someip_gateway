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

// Offers /event_exchange/tx and subscribes to /event_exchange/rx for the gateway integration test.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include "score/mw/com/runtime.h"
#include "score/mw/com/types.h"
#include "score/mw/log/logging.h"
#include "tests/integration_test/event_exchange/service_interface.h"

namespace {

using namespace std::chrono_literals;

template <typename Trait>
class PingService : public Trait::Base {
   public:
    using Trait::Base::Base;
    typename Trait::template Event<score::event_exchange::PingPayload> ping_{
        *this, std::string{score::event_exchange::kEventName}};
};
using PingServiceSkeleton = score::mw::com::AsSkeleton<PingService>;
using PingServiceProxy = score::mw::com::AsProxy<PingService>;

struct AppOptions {
    std::uint64_t tx_count = 20;
    // Number of consecutive RX samples the test requires.
    std::uint64_t rx_count_required = 5;
    std::chrono::milliseconds tx_interval{50};
    std::chrono::milliseconds overall_timeout{30000};
    std::chrono::milliseconds find_service_interval{100};
    std::string ready_file{"/tmp/event_exchange_ready"};
    std::string go_file{"/tmp/event_exchange_go"};
};

AppOptions parse_options(int argc, const char* argv[]) {
    AppOptions opts;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string_view key{argv[i]};
        const std::string_view value{argv[i + 1]};
        if (key == "--tx-count") {
            opts.tx_count = std::strtoull(value.data(), nullptr, 10);
            ++i;
        } else if (key == "--rx-count") {
            opts.rx_count_required = std::strtoull(value.data(), nullptr, 10);
            ++i;
        } else if (key == "--tx-interval-ms") {
            opts.tx_interval = std::chrono::milliseconds{std::strtoll(value.data(), nullptr, 10)};
            ++i;
        } else if (key == "--timeout-ms") {
            opts.overall_timeout =
                std::chrono::milliseconds{std::strtoll(value.data(), nullptr, 10)};
            ++i;
        } else if (key == "--ready-file") {
            opts.ready_file = std::string{value};
            ++i;
        } else if (key == "--go-file") {
            opts.go_file = std::string{value};
            ++i;
        }
    }
    return opts;
}

std::vector<score::safecpp::zstring_view> to_zstring_view_span(int argc, const char* argv[]) {
    std::vector<score::safecpp::zstring_view> out;
    out.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        out.emplace_back(argv[i], std::char_traits<char>::length(argv[i]));
    }
    return out;
}

bool write_ready_marker(const std::string& path) {
    std::ofstream marker{path, std::ios::out | std::ios::trunc};
    if (!marker.is_open()) {
        return false;
    }
    marker << "ready\n";
    marker.flush();
    return static_cast<bool>(marker);
}

}  // namespace

int main(int argc, const char* argv[]) {
    using namespace score::event_exchange;

    const auto opts = parse_options(argc, argv);
    const auto deadline = std::chrono::steady_clock::now() + opts.overall_timeout;

    auto argv_span = to_zstring_view_span(argc, argv);
    score::mw::com::runtime::InitializeRuntime(
        score::cpp::span<score::safecpp::zstring_view>{argv_span.data(), argv_span.size()});

    // ---------- TX (skeleton) ----------
    const auto tx_specifier =
        score::mw::com::InstanceSpecifier::Create(std::string{kTxInstanceSpecifier});
    if (!tx_specifier.has_value()) {
        score::mw::log::LogFatal("event_exchange") << "invalid TX instance specifier";
        return EXIT_FAILURE;
    }
    auto tx_skeleton_res = PingServiceSkeleton::Create(tx_specifier.value());
    if (!tx_skeleton_res.has_value()) {
        score::mw::log::LogFatal("event_exchange") << "TX Skeleton::Create failed";
        return EXIT_FAILURE;
    }
    auto& tx_skeleton = tx_skeleton_res.value();
    if (!tx_skeleton.OfferService().has_value()) {
        score::mw::log::LogFatal("event_exchange") << "TX OfferService failed";
        return EXIT_FAILURE;
    }

    // ---------- RX (proxy) ----------
    const auto rx_specifier =
        score::mw::com::InstanceSpecifier::Create(std::string{kRxInstanceSpecifier});
    if (!rx_specifier.has_value()) {
        score::mw::log::LogFatal("event_exchange") << "invalid RX instance specifier";
        return EXIT_FAILURE;
    }

    score::Result<score::mw::com::ServiceHandleContainer<PingServiceProxy::HandleType>> handles_res;
    while (std::chrono::steady_clock::now() < deadline) {
        handles_res = PingServiceProxy::FindService(rx_specifier.value());
        if (handles_res.has_value() && !handles_res.value().empty()) {
            break;
        }
        std::this_thread::sleep_for(opts.find_service_interval);
    }
    if (!handles_res.has_value() || handles_res.value().empty()) {
        score::mw::log::LogFatal("event_exchange") << "RX FindService timed out";
        return EXIT_FAILURE;
    }
    auto rx_proxy_res = PingServiceProxy::Create(handles_res.value()[0]);
    if (!rx_proxy_res.has_value()) {
        score::mw::log::LogFatal("event_exchange") << "RX Proxy::Create failed";
        return EXIT_FAILURE;
    }
    auto& rx_proxy = rx_proxy_res.value();
    constexpr std::size_t kMaxSampleSlots = 10;
    if (!rx_proxy.ping_.Subscribe(kMaxSampleSlots).has_value()) {
        score::mw::log::LogFatal("event_exchange") << "RX Subscribe failed";
        return EXIT_FAILURE;
    }

    if (!write_ready_marker(opts.ready_file)) {
        score::mw::log::LogFatal("event_exchange")
            << "could not write ready marker: " << opts.ready_file;
        return EXIT_FAILURE;
    }

    // ---------- Combined loop ----------
    std::uint64_t tx_sent = 0;
    std::uint64_t rx_next_expected_counter = 0;
    std::unordered_set<std::uint64_t> rx_seen_counters;
    bool rx_samples_valid = true;
    bool exchange_failed = false;
    bool tx_enabled = opts.tx_count == 0;
    auto next_tx_at = std::chrono::steady_clock::now();

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            score::mw::log::LogError("event_exchange")
                << "timeout: sent " << tx_sent << "/" << opts.tx_count << ", received "
                << rx_seen_counters.size() << "/" << opts.rx_count_required;
            break;
        }

        if (!tx_enabled) {
            std::error_code ec;
            tx_enabled = std::filesystem::exists(opts.go_file, ec);
            if (ec) {
                score::mw::log::LogError("event_exchange")
                    << "could not check TX go-file: " << opts.go_file;
                exchange_failed = true;
                break;
            }
        }

        if (tx_enabled && tx_sent < opts.tx_count && now >= next_tx_at) {
            auto sample_res = tx_skeleton.ping_.Allocate();
            if (!sample_res.has_value()) {
                score::mw::log::LogFatal("event_exchange") << "TX Allocate failed";
                exchange_failed = true;
                break;
            }
            fill_sample(*sample_res.value(), tx_sent);
            if (!tx_skeleton.ping_.Send(std::move(sample_res.value())).has_value()) {
                score::mw::log::LogFatal("event_exchange") << "TX Send failed";
                exchange_failed = true;
                break;
            }
            ++tx_sent;
            next_tx_at = now + opts.tx_interval;
        }

        const auto get_res = rx_proxy.ping_.GetNewSamples(
            [&rx_seen_counters, &rx_next_expected_counter,
             &rx_samples_valid](score::mw::com::SamplePtr<PingPayload> sample) {
                if (const auto counter =
                        matched_expected_counter(*sample, rx_next_expected_counter)) {
                    rx_seen_counters.insert(*counter);
                    ++rx_next_expected_counter;
                } else {
                    rx_samples_valid = false;
                    score::mw::log::LogError("event_exchange")
                        << "invalid RX sample for counter " << rx_next_expected_counter;
                }
            },
            kMaxSampleSlots);
        if (!get_res.has_value()) {
            score::mw::log::LogFatal("event_exchange") << "GetNewSamples failed";
            exchange_failed = true;
            break;
        }
        if (!rx_samples_valid) {
            exchange_failed = true;
            break;
        }

        if (tx_sent >= opts.tx_count && rx_seen_counters.size() >= opts.rx_count_required) {
            break;
        }

        std::this_thread::sleep_for(10ms);
    }

    const bool ok = !exchange_failed && tx_sent >= opts.tx_count &&
                    rx_seen_counters.size() >= opts.rx_count_required;
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
