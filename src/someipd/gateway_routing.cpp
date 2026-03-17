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

#include "src/someipd/gateway_routing.h"

#include <cstring>
#include <thread>

#include "score/mw/log/logging.h"

namespace score::someip_gateway::someipd {

GatewayRouting::GatewayRouting(std::unique_ptr<INetworkStack> network_stack,
                               std::unique_ptr<IInternalIpc> internal_ipc, SomeipDConfig config)
    : config_(std::move(config)),
      internal_ipc_(std::move(internal_ipc)),
      network_stack_(std::move(network_stack)) {}

void GatewayRouting::SetupSubscriptions() {
    for (const auto& svc : config_.subscribed_services) {
        for (const auto& ev : svc.events) {
            score::mw::log::LogInfo() << "Registering message handler for service "
                                      << score::mw::log::LogHex16{svc.service_id} << ":"
                                      << score::mw::log::LogHex16{svc.instance_id} << " event "
                                      << score::mw::log::LogHex16{ev.event_id};

            network_stack_->RegisterMessageHandler(
                svc.service_id, svc.instance_id, ev.event_id,
                [this](ServiceId /*service_id*/, InstanceId /*instance_id*/, EventId /*event_id*/,
                       const std::byte* payload_data, std::size_t payload_size) {
                    const std::size_t total_size = kSomeipFullHeaderSize + payload_size;
                    if (total_size > kMaxMessageSize) {
                        score::mw::log::LogError()
                            << "Message too large (" << static_cast<std::uint32_t>(total_size)
                            << " bytes). Dropping.";
                        return;
                    }
                    std::byte buffer[kMaxMessageSize] = {};
                    std::memcpy(buffer + kSomeipFullHeaderSize, payload_data, payload_size);
                    internal_ipc_->SendToGatewayd(buffer, total_size);
                });
        }

        network_stack_->RequestService(svc.service_id, svc.instance_id);
        for (const auto& ev : svc.events) {
            network_stack_->SubscribeEvent(svc.service_id, svc.instance_id, ev.event_id,
                                           ev.eventgroup_id);
        }
    }
}

void GatewayRouting::SetupOfferings() {
    // Order matters: offer_event must come before offer_service.
    for (const auto& svc : config_.offered_services) {
        for (const auto& ev : svc.events) {
            score::mw::log::LogInfo()
                << "Offering event " << score::mw::log::LogHex16{svc.service_id} << ":"
                << score::mw::log::LogHex16{svc.instance_id} << " event "
                << score::mw::log::LogHex16{ev.event_id} << " in event group "
                << score::mw::log::LogHex16{ev.eventgroup_id};
            network_stack_->OfferEvent(svc.service_id, svc.instance_id, ev.event_id,
                                       ev.eventgroup_id);
        }
        score::mw::log::LogInfo() << "Offering service " << score::mw::log::LogHex16{svc.service_id}
                                  << ":" << score::mw::log::LogHex16{svc.instance_id};
        network_stack_->OfferService(svc.service_id, svc.instance_id);
    }
}

InstanceId GatewayRouting::LookupInstanceId(ServiceId service_id) const {
    for (const auto& svc : config_.offered_services) {
        if (svc.service_id == service_id) {
            return svc.instance_id;
        }
    }
    return kAnyInstance;
}
// exchange event data
void GatewayRouting::ProcessMessages(std::atomic<bool>& shutdown_requested) {
    static constexpr std::size_t kMaxSampleCount = 10;

    score::mw::log::LogInfo() << "SOME/IP daemon started, waiting for messages...";

    while (!shutdown_requested.load()) {
        // TODO: Use ReceiveHandler + async runtime instead of polling
        internal_ipc_->ReceiveFromGatewayd(
            [this](const std::byte* data, std::size_t size) {
                if (size < kSomeipFullHeaderSize) {
                    score::mw::log::LogError()
                        << "Received too small sample (size: " << static_cast<std::uint32_t>(size)
                        << ", expected at least: "
                        << static_cast<std::uint32_t>(kSomeipFullHeaderSize)
                        << "). Skipping message.";
                    return;
                }

                // Read service_id and event_id from the SOME/IP header (big-endian)
                const auto service_id = static_cast<ServiceId>(
                    (std::to_integer<uint16_t>(data[0]) << 8) | std::to_integer<uint16_t>(data[1]));
                const auto event_id = static_cast<EventId>(
                    (std::to_integer<uint16_t>(data[2]) << 8) | std::to_integer<uint16_t>(data[3]));

                const auto instance_id = LookupInstanceId(service_id);
                if (instance_id == kAnyInstance) {
                    score::mw::log::LogError()
                        << "No offered service configured for service_id "
                        << score::mw::log::LogHex16{service_id} << ". Dropping message.";
                    return;
                }

                const auto* payload = data + kSomeipFullHeaderSize;
                const auto payload_size = size - kSomeipFullHeaderSize;
                network_stack_->Notify(service_id, instance_id, event_id, payload, payload_size);
            },
            kMaxSampleCount);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    score::mw::log::LogInfo() << "Shutting down SOME/IP daemon...";
}

void GatewayRouting::Run(std::atomic<bool>& shutdown_requested) {
    network_stack_->StartProcessing();
    SetupSubscriptions(); // once per service, not per message
    SetupOfferings();     // once per service, not per message
    ProcessMessages(shutdown_requested);
    network_stack_->StopProcessing();
}

}  // namespace score::someip_gateway::someipd
