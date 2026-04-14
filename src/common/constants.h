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

constexpr std::size_t kSomeipFullHeaderSize = 16;
constexpr std::size_t kMaxMessageSize = 1500;
constexpr InstanceId kAnyInstance = 0xFFFF;
constexpr std::size_t max_sample_count = 10;

}  // namespace score::someip
#endif  // SCORE_SOMEIP_CONSTANTS_H
