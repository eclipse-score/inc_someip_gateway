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
#ifndef SCORE_MW_COM_IPC_BRIDGE_SAMPLE_SENDER_RECEIVER_H
#define SCORE_MW_COM_IPC_BRIDGE_SAMPLE_SENDER_RECEIVER_H

#include "score/mw/com/types.h"
#include <score/optional.hpp>

#include <chrono>
#include <cstddef>

namespace score::gateway {

class EventSenderReceiver {
public:
    int RunAsProxy(const score::mw::com::InstanceSpecifier& instance_specifier,
                   const score::cpp::optional<std::chrono::milliseconds> cycle_time,
                   const std::size_t num_cycles);
};

} // namespace score::gateway

#endif // SCORE_MW_COM_IPC_BRIDGE_SAMPLE_SENDER_RECEIVER_H
