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

#ifndef SRC_SOMEIPD_REMOTE_NETWORK_SERVICE
#define SRC_SOMEIPD_REMOTE_NETWORK_SERVICE

#include <memory>
#include <vector>
#include <vsomeip/vsomeip.hpp>

#include "score/result/result.h"
#include "score/socom/server_connector.hpp"
#include "src/config/mw_someip_config_generated.h"

namespace score::socom {
class Runtime;
}  // namespace score::socom

namespace score::someipd {

/// \brief Represents a service available from a remote ECU via SOME/IP.
/// \details Owns a SOCom server connector that pushes incoming SOME/IP event data to
///          gatewayd's client connectors. vsomeip message handlers are registered via
///          setup_vsomeip(), which must be called once vsomeip has reached ST_REGISTERED.
class RemoteNetworkService {
   public:
    /// \brief Creates a RemoteNetworkService
    /// \param service_instance_config Configuration for the service instance to create
    /// \param service_type_config Configuration for the service type of the instance to create
    /// \param vsomeip_app vsomeip application used to register message handlers and subscriptions
    /// \param socom_runtime SOCom runtime used to create the server connector
    /// \return Result containing the created instance on success, or an error on failure
    static Result<std::unique_ptr<RemoteNetworkService>> Create(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        std::shared_ptr<vsomeip::application> vsomeip_app, socom::Runtime& socom_runtime);

    /// \brief Registers vsomeip message handlers and subscribes to events for this service.
    /// \details Must be called from within the vsomeip ST_REGISTERED state handler.
    void setup_vsomeip();

    RemoteNetworkService(const RemoteNetworkService&) = delete;
    RemoteNetworkService& operator=(const RemoteNetworkService&) = delete;
    RemoteNetworkService(RemoteNetworkService&&) = delete;
    RemoteNetworkService& operator=(RemoteNetworkService&&) = delete;

   private:
    /// \brief Private constructor
    RemoteNetworkService(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        std::shared_ptr<vsomeip::application> vsomeip_app,
        socom::Enabled_server_connector::Uptr server_connector);

    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config_;
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config_;
    std::shared_ptr<vsomeip::application> vsomeip_app_;
    /// SOCom server connector for pushing event data to gatewayd's client connectors.
    /// Declared last so it is destroyed first, ensuring no allocations occur after other members.
    score::socom::Enabled_server_connector::Uptr server_connector_;
};

}  // namespace score::someipd

#endif  // SRC_SOMEIPD_REMOTE_NETWORK_SERVICE
