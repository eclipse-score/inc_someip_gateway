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

void set_signal_handler(int const signal, void (*const handler)(int)) {
    struct sigaction sa{};
    sa.sa_handler = handler;
    auto const rc = sigaction(signal, &sa, nullptr);
    if (rc != 0) {
        throw std::runtime_error("Failed to ignore " + std::to_string(signal) + ": " +
                                 std::strerror(errno));
    }
}

int main(int argc, char** argv) {
    // communication message_passing does not use MSG_NOSIGNAL, so we need to ignore SIGPIPE
    // globally in tests to avoid crashes when writing to closed sockets
    set_signal_handler(SIGPIPE, SIG_IGN);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
