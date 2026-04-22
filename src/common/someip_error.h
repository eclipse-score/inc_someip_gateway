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
#ifndef SCORE_SOMEIP_ERROR_H
#define SCORE_SOMEIP_ERROR_H

#include "score/result/result.h"

namespace score::someip {

enum class Errc : score::result::ErrorCode {
    kInitializationFailed,  ///< Initialization of the SOME/IP stack failed.
    kInvalidConfiguration,  ///< The provided configuration is invalid.
    kRuntimeError,          ///< A runtime error occurred in the SOME/IP stack.
    kMessageTooLarge,       ///< The message exceeds the maximum allowed size.
    kSerializationError,    ///< An error occurred during message serialization or deserialization.
    kServiceNotFound,       ///< The requested service was not found.
    kMethodNotFound,        ///< The requested method was not found.
    kEventNotFound,         ///< The requested event was not found.
    kFieldNotFound,         ///< The requested field was not found.
    kInvalidMessage,        ///< The received message is invalid or malformed.
    kUnknownError           ///< An unknown error occurred.
};

score::result::Error MakeError(const Errc code, const std::string_view message = "");

}  // namespace score::someip

#endif  // SCORE_SOMEIP_ERROR_H
