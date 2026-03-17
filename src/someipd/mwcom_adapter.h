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
#include <optional>
#include <string>

#include "score/mw/com/runtime.h"
#include "src/network_service/interfaces/message_transfer.h"
#include "src/someipd/i_internal_ipc.h"

namespace score::someip_gateway::someipd {

/// Adapter that implements IInternalIpc using the mw::com framework.
///
/// All mw::com-specific headers and API calls are isolated here.
/// To switch to a different IPC framework, provide another IInternalIpc adapter.
class MwcomAdapter : public IInternalIpc {
   public:
    MwcomAdapter(std::string proxy_instance_specifier, std::string skeleton_instance_specifier,
                 std::size_t max_sample_count);

    bool Init(int argc, const char* argv[]) override;
    bool SendToGatewayd(const std::byte* data, std::size_t size) override;
    void ReceiveFromGatewayd(MessageCallback callback, std::size_t max_count) override;

   private:
    using Proxy = score::someip_gateway::network_service::interfaces::message_transfer::
        SomeipMessageTransferProxy;
    using Skeleton = score::someip_gateway::network_service::interfaces::message_transfer::
        SomeipMessageTransferSkeleton;

    std::string proxy_instance_specifier_;
    std::string skeleton_instance_specifier_;
    std::size_t max_sample_count_;
    std::optional<Proxy> proxy_;
    std::optional<Skeleton> skeleton_;
};

}  // namespace score::someip_gateway::someipd
