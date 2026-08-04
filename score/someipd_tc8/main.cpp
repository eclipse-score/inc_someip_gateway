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

#include <getopt.h>

#include <csignal>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vsomeip/vsomeip.hpp>

#include "dut_config.h"
#include "dut_service.h"
#include "score/mw/log/logging.h"

static std::atomic<bool> g_shutdown_requested{false};
static std::shared_ptr<vsomeip::application> g_app;

static void SignalHandler(int /*signal*/) {
    g_shutdown_requested.store(true);
    if (g_app) {
        g_app->stop();
    }
}

static void PrintUsage(const char* prog) {
    score::mw::log::LogInfo() << "Usage: " << std::string_view{prog}
                              << " -c <tc8_dut_config.json>\n"
                              << "  -c/--config  Path to the DUT service interface config (JSON)\n"
                              << "  -h/--help    Show this help\n";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    const char* const short_opts = "hc:";
    const option long_opts[] = {{"help", no_argument, nullptr, 'h'},
                                {"config", required_argument, nullptr, 'c'},
                                {nullptr, no_argument, nullptr, 0}};

    std::string config_path;
    while (true) {
        const int opt = getopt_long(argc, argv, short_opts, long_opts, nullptr);
        if (opt == -1) {
            break;
        }
        switch (static_cast<char>(opt)) {
            case 'h': {
                PrintUsage(argv[0]);
                return 0;
            }
            case 'c': {
                config_path = optarg;
                break;
            }
            default: {
                PrintUsage(argv[0]);
                return EXIT_FAILURE;
            }
        }
    }

    if (config_path.empty()) {
        score::mw::log::LogFatal() << "[tc8_dut] -c <config.json> is required";
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }

    score::mw::log::LogInfo() << "[tc8_dut] Loading config: " << config_path;
    score::Result<score::someipd_tc8::DutConfig> config_result =
        score::someipd_tc8::LoadDutConfig(config_path);
    if (!config_result.has_value()) {
        score::mw::log::LogFatal() << "[tc8_dut] Failed to load config '" << config_path
                                   << "': " << config_result.error().UserMessage();
        return EXIT_FAILURE;
    }
    const score::someipd_tc8::DutConfig& config = config_result.value();

    g_app = vsomeip::runtime::get()->create_application("tc8_dut");
    if (!g_app->init()) {
        score::mw::log::LogFatal() << "[tc8_dut] vsomeip application init() failed";
        return EXIT_FAILURE;
    }

    // DutService::Start() must be called before the blocking `app->start()` call.
    score::someipd_tc8::DutService service(g_app, config);
    service.Start();

    score::mw::log::LogInfo() << "[tc8_dut] Starting vsomeip event loop (blocking)...";

    g_app->start();

    service.Stop();

    score::mw::log::LogInfo() << "[tc8_dut] Shutdown complete.";
    return EXIT_SUCCESS;
}
