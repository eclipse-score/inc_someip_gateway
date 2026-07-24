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

#ifndef SCORE_SOMEIP_CONSTANTS_H
#define SCORE_SOMEIP_CONSTANTS_H

#include "types.h"

namespace score::someip {

// =============================================================================
// SOME/IP related constants
// =============================================================================

// Size of a SOME/IP message header in bytes.
constexpr std::size_t kSomeipFullHeaderSize = 16;
// Ethernet MTU-derived cap on a single SOME/IP message to avoid fragmentation.
constexpr std::size_t kMaxMessageSize = 1500;
// Wildcard instance ID used to match any service instance in find/subscribe calls.
constexpr InstanceId kAnyInstance = 0xFFFF;
// Maximum number of event samples buffered per subscriber before oldest samples are dropped.
constexpr std::size_t kMaxSampleCount = 10;

// =============================================================================
// SOCom IPC bridge constants
// =============================================================================

// Maximum size of IPC messages for the SOCom control channel.
constexpr std::size_t kMaxIpcMessageSize = 32768;

}  // namespace score::someip
#endif  // SCORE_SOMEIP_CONSTANTS_H
