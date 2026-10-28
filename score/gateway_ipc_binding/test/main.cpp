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

#include <gtest/gtest.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <string>

#include "score/filesystem/path.h"
#include "score/mw/com/runtime.h"
#include "score/mw/com/runtime_configuration.h"

namespace {
void set_signal_handler(int const signal, void (*const handler)(int)) {
    struct sigaction sa{};
    sa.sa_handler = handler;
    auto const rc = sigaction(signal, &sa, nullptr);
    if (rc != 0) {
        throw std::runtime_error("Failed to ignore " + std::to_string(signal) + ": " +
                                 std::strerror(errno));
    }
}

std::string manifest_path(int& argc, char** argv) {
    std::string manifest;
    for (int i = 1; i < argc;) {
        if (std::strcmp(argv[i], "--mw-com-config") != 0) {
            ++i;
            continue;
        }
        if ((i + 1 >= argc) || (argv[i + 1][0] == '\0')) {
            throw std::invalid_argument("--mw-com-config requires a path");
        }
        if (!manifest.empty()) {
            throw std::invalid_argument("--mw-com-config may only be specified once");
        }
        manifest = argv[i + 1];
        // remove the --mw-com-config argument and its value from argv
        // argv will be parsed by Google Test after this function returns
        for (int j = i; j + 2 < argc; ++j) {
            argv[j] = argv[j + 2];
        }
        argc -= 2;
        argv[argc] = nullptr;
    }
    if (manifest.empty()) {
        throw std::invalid_argument("Missing required argument --mw-com-config <path>");
    }
    return manifest;
}
}  // namespace

int main(int argc, char** argv) {
    // communication message_passing does not use MSG_NOSIGNAL, so we need to ignore SIGPIPE
    // globally in tests to avoid crashes when writing to closed sockets
    set_signal_handler(SIGPIPE, SIG_IGN);

    auto const manifest = manifest_path(argc, argv);
    score::mw::com::runtime::InitializeRuntime(
        score::mw::com::runtime::RuntimeConfiguration{score::filesystem::Path{manifest}});

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
