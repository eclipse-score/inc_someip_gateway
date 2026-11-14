/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
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

#ifndef IMPL_SOMEIPD_OPENSOMEIP_HOST
#define IMPL_SOMEIPD_OPENSOMEIP_HOST

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "platform/buffer_pool.h"
#include "score/config/mw_someip_config_generated.h"

namespace score::someipd {

/// \brief Sender the opensomeip transport already reported.
struct ObservedSender {
    std::string address;
    std::uint16_t port{0};
};

/// \brief Header fields of one received notification. Payload bytes travel separately.
struct ObservedEvent {
    std::uint16_t service_id{0};
    std::uint16_t event_id{0};
    std::uint16_t client_id{0};
    std::uint16_t session_id{0};
    std::uint8_t protocol_version{0x01};
    std::uint8_t interface_version{0x01};
    std::uint8_t message_type{0x02};
    std::uint8_t return_code{0};
};

using RemoteEventHandler =
    std::function<void(ObservedEvent const& event, ::someip::platform::ByteBuffer const& payload,
                       ObservedSender const& sender)>;

/// \brief One remote service someipd consumes through opensomeip.
struct RemoteServiceRequest {
    std::uint16_t service_id{0};
    std::uint16_t instance_id{0};
    std::uint8_t major_version{1};
    std::uint32_t minor_version{0};
    std::vector<std::uint16_t> event_ids;
    std::vector<std::uint16_t> eventgroup_ids;
    std::vector<std::uint16_t> method_ids;
    RemoteEventHandler on_event;
    /// \brief Set by OpensomeipHost once find/subscribe has been sent.
    bool armed{false};
};

/// \brief In-process opensomeip composition owned by Routing.
///
/// someipd creates the sockets, registers each configured service on one shared demux,
/// and calls offer, notify, subscribe, and methods. Service-discovery timing comes from
/// the gateway vsomeip JSON.
class OpensomeipHost {
   public:
    OpensomeipHost();
    ~OpensomeipHost();

    OpensomeipHost(const OpensomeipHost&) = delete;
    OpensomeipHost& operator=(const OpensomeipHost&) = delete;
    OpensomeipHost(OpensomeipHost&&) = delete;
    OpensomeipHost& operator=(OpensomeipHost&&) = delete;

    /// \brief Binds the sockets and starts service discovery. Does not offer services.
    bool start();

    /// \brief Stops discovery and the shared demux. Callbacks have returned.
    void stop();

    /// \brief Offers the local instances in @p config and registers their methods.
    void offer(mw_someip_config::Root const& config);

    /// \brief Publishes one event. Payload bytes are copied unchanged.
    void publish_event(std::uint16_t service_id, std::uint16_t instance_id, std::uint16_t event_id,
                       ::someip::platform::ByteBuffer const& payload);

    /// \brief Finds @p request and subscribes to its eventgroups.
    void subscribe_remote(RemoteServiceRequest request);

    /// \brief Calls a remote method. Request bytes are sent unchanged.
    /// \return True when the response arrived.
    bool call_method(std::uint16_t service_id, std::uint16_t instance_id, std::uint16_t method_id,
                     ::someip::platform::ByteBuffer const& payload);

   private:
    struct Objects;
    void on_subscription(std::uint16_t service_id, std::uint16_t instance_id,
                         std::uint16_t eventgroup_id, std::string address, std::uint16_t port,
                         bool send_initial);
    void arm_remote(RemoteServiceRequest& request);
    void on_remote_found(RemoteServiceRequest& request, std::string const& address,
                         std::uint16_t port);

    std::unique_ptr<Objects> objects_;
};

}  // namespace score::someipd

#endif  // IMPL_SOMEIPD_OPENSOMEIP_HOST
