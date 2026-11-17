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
#include <cstddef>
#include <string_view>

namespace score::gateway_ipc_binding {
namespace {

class ForeignErrorDomain final : public score::result::ErrorDomain {
   public:
    std::string_view MessageFor(score::result::ErrorCode const& /*code*/) const noexcept override {
        return "Foreign domain error";
    }
};

constexpr ForeignErrorDomain kForeignDomain{};

constexpr std::size_t kActiveEnumeratorCount = 11U;
constexpr std::string_view kEmptyUserMessage{""};
constexpr std::string_view kCustomUserMessage{"gateway_ipc_binding diagnostic context"};
constexpr std::string_view kUnknownErrorMessage{"Unknown error"};

constexpr std::array<Mw_com_binding_error, kActiveEnumeratorCount> kActiveEnumerators{
    Mw_com_binding_error::logic_error_invalid_instance_specifier,
    Mw_com_binding_error::runtime_error_someipd_service_creation_failed,
    Mw_com_binding_error::runtime_error_someipd_service_offer_failed,
    Mw_com_binding_error::runtime_error_someipd_service_find_failed,
    Mw_com_binding_error::logic_error_already_started,
    Mw_com_binding_error::logic_error_invalid_service_configuration,
    Mw_com_binding_error::logic_error_unknown_event,
    Mw_com_binding_error::runtime_error_service_setup_failed,
    Mw_com_binding_error::runtime_error_service_find_failed,
    Mw_com_binding_error::runtime_error_sample_size_mismatch,
    Mw_com_binding_error::runtime_error_sample_allocation_failed,
};

static_assert(noexcept(MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier)));
static_assert(noexcept(MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier,
                                 kCustomUserMessage)));

TEST(ErrorTest, DefinedEnumeratorsShareDomainAndMapToDistinctMessages) {
    ASSERT_EQ(kActiveEnumerators.size(), kActiveEnumeratorCount);

    for (std::size_t i = 0U; i < kActiveEnumerators.size(); ++i) {
        SCOPED_TRACE(i);
        auto const code = kActiveEnumerators[i];
        auto const raw_code = static_cast<score::result::ErrorCode>(code);
        auto const err_i = MakeError(code);
        auto const err_i_with_context = MakeError(code, kCustomUserMessage);
        score::result::Error const adl_constructed{code};
        score::result::Error const foreign_same_code{raw_code, kForeignDomain, kEmptyUserMessage};

        // score::result::Error::operator== compares both ErrorCode and singleton ErrorDomain
        // address. Matching repeated MakeError calls while rejecting a foreign domain with the
        // identical ErrorCode verifies every call binds to the same singleton ErrorDomain.
        EXPECT_EQ(*err_i, raw_code);
        EXPECT_EQ(err_i, MakeError(code));
        EXPECT_EQ(err_i, adl_constructed);
        EXPECT_EQ(err_i, err_i_with_context);
        EXPECT_FALSE(err_i != MakeError(code));
        EXPECT_NE(err_i, foreign_same_code);
        EXPECT_EQ(err_i.UserMessage(), kEmptyUserMessage);
        EXPECT_EQ(err_i_with_context.UserMessage(), kCustomUserMessage);
        EXPECT_EQ(err_i_with_context.Message(), err_i.Message());
        EXPECT_NE(err_i.Message(), kEmptyUserMessage);
        EXPECT_NE(err_i.Message(), kUnknownErrorMessage);

        for (std::size_t j = i + 1U; j < kActiveEnumerators.size(); ++j) {
            SCOPED_TRACE(j);
            auto const err_j = MakeError(kActiveEnumerators[j]);
            EXPECT_NE(err_i, err_j);
            EXPECT_FALSE(err_i == err_j);
            EXPECT_NE(err_i.Message(), err_j.Message());
        }
    }
}

TEST(ErrorTest, OutOfRangeCodesReturnUnknownErrorAndPreserveUserMessage) {
    constexpr score::result::ErrorCode kBelowFirstEnumerator =
        static_cast<score::result::ErrorCode>(
            Mw_com_binding_error::logic_error_invalid_instance_specifier) -
        1;
    constexpr score::result::ErrorCode kPastLastEnumerator =
        static_cast<score::result::ErrorCode>(
            Mw_com_binding_error::runtime_error_sample_allocation_failed) +
        1;
    constexpr score::result::ErrorCode kMaxByteErrorCode = 255;
    constexpr score::result::ErrorCode kLargeErrorCode = 999;
    constexpr std::size_t kOutOfRangeCount = 4U;

    constexpr std::array<score::result::ErrorCode, kOutOfRangeCount> kOutOfRangeCodes{
        kBelowFirstEnumerator,
        kPastLastEnumerator,
        kMaxByteErrorCode,
        kLargeErrorCode,
    };

    ASSERT_EQ(kOutOfRangeCodes.size(), kOutOfRangeCount);
    auto const reference_error =
        MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier);

    for (score::result::ErrorCode const raw_code : kOutOfRangeCodes) {
        SCOPED_TRACE(raw_code);
        auto const err = MakeError(static_cast<Mw_com_binding_error>(raw_code), kCustomUserMessage);
        score::result::Error const foreign_same_code{raw_code, kForeignDomain, kEmptyUserMessage};

        EXPECT_EQ(*err, raw_code);
        EXPECT_EQ(err, MakeError(static_cast<Mw_com_binding_error>(raw_code)));
        EXPECT_NE(err, foreign_same_code);
        EXPECT_EQ(err.Message(), kUnknownErrorMessage);
        EXPECT_EQ(err.UserMessage(), kCustomUserMessage);
        EXPECT_NE(err, reference_error);
    }
}

}  // namespace
}  // namespace score::gateway_ipc_binding
