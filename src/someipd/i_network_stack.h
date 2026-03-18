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

#include "src/someipd/someipd_types.h"

namespace score::someip_gateway::someipd {

/// Abstract interface for a SOME/IP network stack (e.g., vsomeip, Xsomeip, etc.).
///
/// Adapters implement this interface to isolate the core routing logic from the
/// concrete SOME/IP stack being used.  Swapping the network stack only requires
/// providing a different adapter — no changes to GatewayRouting or main.
class INetworkStack {
   public:
    virtual ~INetworkStack() = default;

    /// Initialize the network stack (create application, configure transport, etc.).
    virtual bool Init() = 0;

    /// Start processing network messages (non-blocking).
    /// After this call, registered message handlers may be invoked.
    virtual void StartProcessing() = 0;

    /// Stop processing network messages and release resources.
    virtual void StopProcessing() = 0;

    // -- Offering services to the SOME/IP network (outbound: IPC → network) --

    virtual void OfferEvent(ServiceId service_id, InstanceId instance_id, EventId event_id,
                            EventGroupId eventgroup_id) = 0;

    virtual void OfferService(ServiceId service_id, InstanceId instance_id) = 0;

    /// Send an event notification to the SOME/IP network.
    virtual void Notify(ServiceId service_id, InstanceId instance_id, EventId event_id,
                        const std::byte* payload_data, std::size_t payload_size) = 0;

    // -- Subscribing to services from the SOME/IP network (inbound: network → IPC) --

    virtual void RequestService(ServiceId service_id, InstanceId instance_id) = 0;

    virtual void SubscribeEvent(ServiceId service_id, InstanceId instance_id, EventId event_id,
                                EventGroupId eventgroup_id) = 0;

    /// Callback signature for received SOME/IP messages.
    using MessageHandler =
        std::function<void(ServiceId service_id, InstanceId instance_id, EventId event_id,
                           const std::byte* payload_data, std::size_t payload_size)>;

    /// Register a handler that will be called when a matching message arrives.
    virtual void RegisterMessageHandler(ServiceId service_id, InstanceId instance_id,
                                        EventId event_id, MessageHandler handler) = 0;
};

}  // namespace score::someip_gateway::someipd
