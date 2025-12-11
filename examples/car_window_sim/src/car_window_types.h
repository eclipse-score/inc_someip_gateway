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

namespace car_window_types {

enum class WindowState : std::uint32_t {
    Stopped = 0U,
    Opening = 1U,
    Closing = 2U,
    Open = 3U,
    Closed = 4U
};

enum class WindowCommand : std::uint32_t {
    Stop = 0U,
    Open = 1U,
    Close = 2U,
};

struct WindowInfo {
    std::uint32_t pos;
    WindowState state;
};

struct WindowControl {
    WindowCommand command;
};

template <typename Trait>
class WindowInfoInterface : public Trait::Base {
   public:
    using Trait::Base::Base;

    typename Trait::template Event<WindowInfo> window_info_{*this, "window_info"};
};

template <typename Trait>
class WindowControlInterface : public Trait::Base {
   public:
    using Trait::Base::Base;

    typename Trait::template Event<WindowControl> window_control_{*this, "window_control"};
};

using WindowInfoProxy = score::mw::com::AsProxy<WindowInfoInterface>;
using WindowInfoSkeleton = score::mw::com::AsSkeleton<WindowInfoInterface>;
using WindowControlProxy = score::mw::com::AsProxy<WindowControlInterface>;
using WindowControlSkeleton = score::mw::com::AsSkeleton<WindowControlInterface>;

}  // namespace car_window_types

#endif  // SCORE_IPC_BRIDGE_DATATYPE_H
