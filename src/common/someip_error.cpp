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

#include "someip_error.h"

namespace score::someip {

std::string_view SomeipErrorDomain::MessageFor(const score::result::ErrorCode& code) const noexcept {
    switch (static_cast<Errc>(code)) {
        // coverity[autosar_cpp14_m6_4_5_violation]
        case Errc::kInitializationFailed:
            return "Initialization of the SOME/IP stack failed.";
        case Errc::kInvalidConfiguration:
            return "The provided configuration is invalid.";
        case Errc::kRuntimeError:
            return "A runtime error occurred in the SOME/IP stack.";
        case Errc::kMessageTooLarge:
            return "The message exceeds the maximum allowed size.";
        case Errc::kSerializationError:
            return "An error occurred during message serialization or deserialization.";
        case Errc::kServiceNotFound:
            return "The requested service was not found.";
        case Errc::kMethodNotFound:
            return "The requested method was not found.";
        case Errc::kEventNotFound:
            return "The requested event was not found.";
        case Errc::kFieldNotFound:
            return "The requested field was not found.";
        case Errc::kInvalidMessage:
            return "The received message is invalid or malformed.";
        case Errc::kUnknownError:
            return "An unknown error occurred.";
        default:
            return "Unknown SOME/IP error.";
    }
}

score::result::Error MakeError(const Errc code, const std::string_view message) {
    static constexpr SomeipErrorDomain error_domain;
    return {static_cast<score::result::ErrorCode>(code), error_domain, message};
}

}  // namespace score::someip
