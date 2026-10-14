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

#include <memory>
#include <mutex>
#include <vector>
#include <vsomeip/vsomeip.hpp>

#include "score/config/mw_someip_config_generated.h"
#include "score/result/result.h"
#include "score/socom/server_connector.hpp"

namespace score::socom {
class Runtime;
}  // namespace score::socom

namespace score::someipd {

/// \brief Represents a service available from a remote ECU via SOME/IP.
/// \details Owns a SOCom server connector that pushes incoming SOME/IP event data to
///          gatewayd's client connectors. vsomeip message handlers are registered via
///          setup_vsomeip(), which must be called once vsomeip has reached ST_REGISTERED.
///          The SOCom service mirrors the remote service's vsomeip availability: enabled while
///          it is available, disabled otherwise (also from creation until the first report).
class RemoteNetworkService {
   public:
    /// \brief Creates a RemoteNetworkService
    /// \param service_instance_config Configuration for the service instance to create
    /// \param service_type_config Configuration for the service type of the instance to create
    /// \param vsomeip_app vsomeip application used to register message handlers and subscriptions
    /// \param socom_runtime SOCom runtime used to create the server connector
    /// \param socom_interface SOCom interface of the server connector
    /// \return Result containing the created instance on success, or an error on failure
    static Result<std::unique_ptr<RemoteNetworkService>> Create(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        std::shared_ptr<vsomeip::application> vsomeip_app, socom::Runtime& socom_runtime,
        socom::Service_interface_identifier const& socom_interface);

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
        socom::Disabled_server_connector::Uptr disabled_server_connector);

    /// \brief Enables the SOCom service when vsomeip reports the remote service available,
    ///        disables it when vsomeip reports it not available.
    void on_availability_change(bool is_available);

    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config_;
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config_;
    std::shared_ptr<vsomeip::application> vsomeip_app_;
    /// Serializes switching the SOCom service on or off (availability handler) against event
    /// forwarding (message handler); vsomeip may run the two on different threads.
    std::mutex server_connector_mutex_;
    /// The SOCom server connector while it is disabled, null while it is enabled.
    score::socom::Disabled_server_connector::Uptr disabled_server_connector_;
    /// SOCom server connector for pushing event data to gatewayd's client connectors, null while
    /// disabled. Declared last so it is destroyed first, ensuring no allocations occur after other
    /// members.
    score::socom::Enabled_server_connector::Uptr server_connector_;
};

}  // namespace score::someipd

#endif  // IMPL_SOMEIPD_REMOTE_NETWORK_SERVICE
