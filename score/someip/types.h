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

#ifndef SCORE_SOMEIP_TYPES_H
#define SCORE_SOMEIP_TYPES_H

#include <cstddef>
#include <cstdint>

namespace score::someip {

using ServiceId = std::uint16_t;
using InstanceId = std::uint16_t;
using EventId = std::uint16_t;
using EventGroupId = std::uint16_t;

}  // namespace score::someip

#endif  // SCORE_SOMEIP_TYPES_H
