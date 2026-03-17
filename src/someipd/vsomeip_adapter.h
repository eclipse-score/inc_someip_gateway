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

#include <memory>
#include <string>
#include <thread>
#include <vsomeip/vsomeip.hpp>

#include "src/someipd/i_network_stack.h"

namespace score::someip_gateway::someipd {

/// Adapter that implements INetworkStack using the vsomeip library.
///
/// All vsomeip-specific headers and API calls are isolated here.
/// To switch to a different SOME/IP stack, provide another INetworkStack adapter.
class VsomeipAdapter : public INetworkStack {
   public:
    explicit VsomeipAdapter(std::string app_name);
    ~VsomeipAdapter() override;

    VsomeipAdapter(const VsomeipAdapter&) = delete;
    VsomeipAdapter& operator=(const VsomeipAdapter&) = delete;

    bool Init() override;
    void StartProcessing() override;
    void StopProcessing() override;

    void OfferEvent(ServiceId service_id, InstanceId instance_id, EventId event_id,
                    EventGroupId eventgroup_id) override;
    void OfferService(ServiceId service_id, InstanceId instance_id) override;
    void Notify(ServiceId service_id, InstanceId instance_id, EventId event_id,
                const std::byte* payload_data, std::size_t payload_size) override;

    void RequestService(ServiceId service_id, InstanceId instance_id) override;
    void SubscribeEvent(ServiceId service_id, InstanceId instance_id, EventId event_id,
                        EventGroupId eventgroup_id) override;
    void RegisterMessageHandler(ServiceId service_id, InstanceId instance_id, EventId event_id,
                                MessageHandler handler) override;

   private:
    std::string app_name_;
    std::shared_ptr<vsomeip::application> application_;
    std::shared_ptr<vsomeip::payload> payload_;
    std::thread processing_thread_;
};

}  // namespace score::someip_gateway::someipd
