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
#ifndef SCORE_IPC_BRIDGE_DATATYPE_H
#define SCORE_IPC_BRIDGE_DATATYPE_H

#include "score/mw/com/types.h"

#include <array>
#include <cstdint>

namespace score::gateway {

struct MapApiLanesStamped {
    // Must be null terminated
    std::array<std::uint8_t, 101U> string_data;
};

template<typename Trait>
class IpcBridgeInterface : public Trait::Base {
public:
    using Trait::Base::Base;

    typename Trait::template Event<MapApiLanesStamped> map_api_lanes_stamped_{
        *this, "map_api_lanes_stamped"};
};

using IpcBridgeProxy = score::mw::com::AsProxy<IpcBridgeInterface>;
using IpcBridgeSkeleton = score::mw::com::AsSkeleton<IpcBridgeInterface>;

} // namespace score::gateway

#endif // SCORE_IPC_BRIDGE_DATATYPE_H
