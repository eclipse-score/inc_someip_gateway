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

#ifndef SRC_SOMEIPD_LOCAL_NETWORK_SERVICE
#define SRC_SOMEIPD_LOCAL_NETWORK_SERVICE

#include <memory>
#include <vector>
#include <vsomeip/vsomeip.hpp>

#include "score/socom/client_connector.hpp"
#include "src/config/mw_someip_config_generated.h"

namespace score::socom {
class Runtime;
}  // namespace score::socom

namespace score::someipd {

/// \brief Represents a service offered locally (by an app behind gatewayd) on the SOME/IP network.
/// \details Owns a SOCom client connector that receives event updates from gatewayd's server
///          connector and forwards them to the SOME/IP network via vsomeip notify().
class LocalNetworkService {
   public:
    /// \brief Constructs a LocalNetworkService
    /// \param service_instance_config Configuration for this service instance
    /// \param service_type_config Configuration for the service type of this instance
    /// \param vsomeip_app vsomeip application used to notify events on the SOME/IP network
    /// \param socom_runtime SOCom runtime used to create the client connector
    LocalNetworkService(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        std::shared_ptr<vsomeip::application> vsomeip_app, socom::Runtime& socom_runtime);

    /// \brief Creates a LocalNetworkService and adds it to the instances vector
    /// \param service_instance_config Configuration for the service instance to create
    /// \param service_type_config Configuration for the service type of the instance to create
    /// \param vsomeip_app vsomeip application used to notify events on the SOME/IP network
    /// \param socom_runtime SOCom runtime used to create the client connector
    /// \param instances Vector to which the created LocalNetworkService is appended
    static void Create(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        std::shared_ptr<vsomeip::application> vsomeip_app, socom::Runtime& socom_runtime,
        std::vector<std::unique_ptr<LocalNetworkService>>& instances);

    LocalNetworkService(const LocalNetworkService&) = delete;
    LocalNetworkService& operator=(const LocalNetworkService&) = delete;
    LocalNetworkService(LocalNetworkService&&) = delete;
    LocalNetworkService& operator=(LocalNetworkService&&) = delete;

   private:
    void forward_to_vsomeip(socom::Event_id event_id, socom::Payload payload);

    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config_;
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config_;
    std::shared_ptr<vsomeip::application> vsomeip_app_;
    /// Declared last so it is destroyed first, ensuring no callbacks fire after the other members.
    socom::Client_connector::Uptr client_connector_;
};

}  // namespace score::someipd

#endif  // SRC_SOMEIPD_LOCAL_NETWORK_SERVICE
