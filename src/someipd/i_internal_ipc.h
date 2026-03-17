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
#include <functional>

namespace score::someip_gateway::someipd {

/// Abstract interface for internal IPC between someipd and gatewayd.
///
/// Adapters implement this interface to isolate the core routing logic from the
/// concrete IPC framework being used (e.g., mw::com, plain sockets, etc.).
class IInternalIpc {
   public:
    virtual ~IInternalIpc() = default;

    /// Initialize the IPC runtime, discover services, and establish connections.
    virtual void Init(int argc, const char* argv[]) = 0;

    /// Send a raw SOME/IP message to gatewayd (inbound path: network → someipd → gatewayd).
    virtual bool SendToGatewayd(const std::byte* data, std::size_t size) = 0;

    /// Callback signature for messages received from gatewayd.
    using MessageCallback = std::function<void(const std::byte* data, std::size_t size)>;

    /// Poll for new messages from gatewayd (outbound path: gatewayd → someipd → network).
    virtual void ReceiveFromGatewayd(MessageCallback callback, std::size_t max_count) = 0;
};

}  // namespace score::someip_gateway::someipd
