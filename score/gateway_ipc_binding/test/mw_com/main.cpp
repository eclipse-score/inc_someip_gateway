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

#include <cstdlib>
#include <iostream>
#include <string>

#include "score/filesystem/path.h"
#include "score/mw/com/runtime.h"
#include "score/mw/com/runtime_configuration.h"

namespace {

/// \brief Locate the mw::com manifest that ships as runfile of this test
std::string manifest_path() {
    char const* const test_srcdir = std::getenv("TEST_SRCDIR");
    char const* const test_workspace = std::getenv("TEST_WORKSPACE");
    if ((test_srcdir == nullptr) || (test_workspace == nullptr)) {
        return {};
    }
    return std::string{test_srcdir} + "/" + test_workspace +
           "/score/gateway_ipc_binding/test/mw_com/mw_com_config.json";
}

}  // namespace

int main(int argc, char** argv) {
    auto const manifest = manifest_path();
    if (manifest.empty()) {
        std::cerr << "TEST_SRCDIR/TEST_WORKSPACE not set, cannot locate mw_com_config.json\n";
        return 1;
    }

    // mw::com must be initialized exactly once per process, before any proxy or skeleton is
    // created. The binding itself does not own the mw::com runtime, the daemons do; here the test
    // binary takes that role.
    score::mw::com::runtime::InitializeRuntime(
        score::mw::com::runtime::RuntimeConfiguration{score::filesystem::Path{manifest}});

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
