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

#include "score/gateway/common.h"

namespace score::mw::com::impl {

std::ostream& operator<<(std::ostream& stream, const InstanceSpecifier& instance_specifier)
{
    stream << instance_specifier.ToString();
    return stream;
}
} // namespace score::mw::com::impl

namespace score::gateway {
Result<score::mw::com::SampleAllocateePtr<MapApiLanesStamped>>
PrepareMapLaneSample(IpcBridgeSkeleton& skeleton, const std::size_t cycle)
{
    auto sample_result = skeleton.map_api_lanes_stamped_.Allocate();
    if (!sample_result.has_value()) {
        return sample_result;
    }
    auto sample = std::move(sample_result).value();

    std::cout << ToString("Sending sample: ", cycle, "\n");
    return sample;
}

} // namespace score::gateway
