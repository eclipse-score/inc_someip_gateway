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

#include "score/gateway_ipc_binding/error.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

namespace score::gateway_ipc_binding {
namespace {

constexpr std::string_view kUnknownErrorMessage{"Unknown error"};
constexpr std::string_view kCustomUserDetail{"user_detail"};
constexpr score::result::ErrorCode kUnmappedErrorCodeValue{999};

constexpr std::array kKnownErrorCodes{
    Mw_com_binding_error::logic_error_invalid_instance_specifier,
    Mw_com_binding_error::runtime_error_someipd_service_creation_failed,
    Mw_com_binding_error::runtime_error_someipd_service_offer_failed,
    Mw_com_binding_error::runtime_error_someipd_service_find_failed,
    Mw_com_binding_error::runtime_error_someipd_service_proxy_creation_failed,
    Mw_com_binding_error::logic_error_already_started,
    Mw_com_binding_error::logic_error_invalid_service_configuration,
    Mw_com_binding_error::logic_error_unknown_event,
    Mw_com_binding_error::runtime_error_service_setup_failed,
    Mw_com_binding_error::runtime_error_service_find_failed,
    Mw_com_binding_error::runtime_error_sample_size_mismatch,
    Mw_com_binding_error::runtime_error_sample_allocation_failed,
};

TEST(ErrorTest, KnownErrorCodesMapToExpectedMessagesAndDefaultEmptyUserMessage) {
    for (auto const err : kKnownErrorCodes) {
        auto const code = MakeError(err);
        EXPECT_EQ(*code, static_cast<score::result::ErrorCode>(err));
        EXPECT_FALSE(code.Message().empty());
        EXPECT_NE(code.Message(), kUnknownErrorMessage);
        EXPECT_TRUE(code.UserMessage().empty());
    }
}

TEST(ErrorTest, MakeErrorPreservesCustomUserMessage) {
    constexpr auto err = Mw_com_binding_error::logic_error_invalid_instance_specifier;
    auto const code = MakeError(err, kCustomUserDetail);
    EXPECT_EQ(*code, static_cast<score::result::ErrorCode>(err));
    EXPECT_EQ(code.UserMessage(), kCustomUserDetail);
}

TEST(ErrorTest, UnmappedErrorCodeReturnsUnknownErrorMessage) {
    auto const code =
        MakeError(static_cast<Mw_com_binding_error>(kUnmappedErrorCodeValue), kCustomUserDetail);
    EXPECT_EQ(*code, kUnmappedErrorCodeValue);
    EXPECT_EQ(code.Message(), kUnknownErrorMessage);
    EXPECT_EQ(code.UserMessage(), kCustomUserDetail);
}

}  // namespace
}  // namespace score::gateway_ipc_binding
