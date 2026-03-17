/********************************************************************************
 * Copyright (c) 2025 Contributors to the Eclipse Foundation
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

#pragma once

#include <cstddef>
#include <cstdint>

namespace score::someip_gateway::someipd {

/// Stack-independent SOME/IP type aliases.
using ServiceId = std::uint16_t;
using InstanceId = std::uint16_t;
using EventId = std::uint16_t;
using EventGroupId = std::uint16_t;

/// SOME/IP protocol constants (per SOME/IP specification, stack-independent).
constexpr std::size_t kSomeipFullHeaderSize = 16;
constexpr std::size_t kMaxMessageSize = 1500;
constexpr InstanceId kAnyInstance = 0xFFFF;

}  // namespace score::someip_gateway::someipd
