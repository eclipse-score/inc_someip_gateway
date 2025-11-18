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

#ifndef SCORE_MW_COM_IPC_BRIDGE_COMMON_H
#define SCORE_MW_COM_IPC_BRIDGE_COMMON_H

#include "score/mw/com/types.h"
#include <score/gateway/datatype.h>

#include <ostream>

namespace score::mw::com::impl {

std::ostream& operator<<(std::ostream& stream, const InstanceSpecifier& instance_specifier);

}

namespace score::gateway {
template<typename T>
void ToStringImpl(std::ostream& o, T t)
{
    o << t;
}

template<typename T, typename... Args>
void ToStringImpl(std::ostream& o, T t, Args... args)
{
    ToStringImpl(o, t);
    ToStringImpl(o, args...);
}

template<typename... Args>
std::string ToString(Args... args)
{
    std::ostringstream oss;
    ToStringImpl(oss, args...);
    return oss.str();
}

Result<score::mw::com::SampleAllocateePtr<MapApiLanesStamped>>
PrepareMapLaneSample(IpcBridgeSkeleton& skeleton, const std::size_t cycle);

} // namespace score::gateway

#endif // SCORE_MW_COM_IPC_BRIDGE_COMMON_H
