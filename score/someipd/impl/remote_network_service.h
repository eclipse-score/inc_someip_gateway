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

#ifndef IMPL_SOMEIPD_REMOTE_NETWORK_SERVICE
#define IMPL_SOMEIPD_REMOTE_NETWORK_SERVICE

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "score/config/mw_someip_config_generated.h"
#include "score/result/result.h"
#include "score/socom/server_connector.hpp"

namespace score::socom {
class Runtime;
}  // namespace score::socom

namespace score::someipd {

class Routing;

/// \brief Represents a service available from a remote ECU via SOME/IP.
/// \details Owns a SOCom server connector that pushes incoming SOME/IP event data to
///          gatewayd's client connectors. Setup() subscribes once the stack is registered.
class RemoteNetworkService {
   public:
    /// \brief Creates a RemoteNetworkService
    /// \param service_instance_config Configuration for the service instance to create
    /// \param service_type_config Configuration for the service type of the instance to create
    /// \param routing Stack that subscribes and calls methods
    /// \param socom_runtime SOCom runtime used to create the server connector
    /// \return Result containing the created instance on success, or an error on failure
    static Result<std::unique_ptr<RemoteNetworkService>> Create(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config, Routing& routing,
        socom::Runtime& socom_runtime);

    /// \brief Subscribes to events for this service.
    /// \details Called once the stack is registered.
    void Setup();

    RemoteNetworkService(const RemoteNetworkService&) = delete;
    RemoteNetworkService& operator=(const RemoteNetworkService&) = delete;
    RemoteNetworkService(RemoteNetworkService&&) = delete;
    RemoteNetworkService& operator=(RemoteNetworkService&&) = delete;

   private:
    /// \brief One received notification, header fields plus the payload bytes that follow them.
    struct NetworkEvent {
        socom::Event_id socom_event_id{};
        std::uint16_t service_id{0};
        std::uint16_t event_id{0};
        std::uint16_t client_id{0};
        std::uint16_t session_id{0};
        std::uint8_t protocol_version{0x01};
        std::uint8_t interface_version{0x01};
        std::uint8_t message_type{0x02};
        std::uint8_t return_code{0};
        void const* payload{nullptr};
        std::size_t payload_size{0};
        std::string sender_address;
        std::uint16_t sender_port{0};
    };

    RemoteNetworkService(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config, Routing& routing,
        socom::Enabled_server_connector::Uptr server_connector);

    /// \brief Forwards @p event onto SOCom. Payload bytes are copied unchanged.
    /// \details The sender is the endpoint the stack reported. Which peers are allowed is decided
    ///          here; the flatbuffer carries no address list, so a reported sender is accepted.
    void deliver_network_event(NetworkEvent const& event);

    bool accept_sender(std::string const& address, std::uint16_t port);

    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config_;
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config_;
    Routing& routing_;
    std::string last_sender_address_;
    std::uint16_t last_sender_port_{0};
    /// SOCom server connector for pushing event data to gatewayd's client connectors.
    /// Declared last so it is destroyed first, ensuring no allocations occur after other members.
    score::socom::Enabled_server_connector::Uptr server_connector_;
};

}  // namespace score::someipd

#endif  // IMPL_SOMEIPD_REMOTE_NETWORK_SERVICE
