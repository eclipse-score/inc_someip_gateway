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

#ifndef IMPL_SOMEIPD_LOCAL_NETWORK_SERVICE
#define IMPL_SOMEIPD_LOCAL_NETWORK_SERVICE

#include <functional>
#include <memory>
#include <vector>
#include <vsomeip/vsomeip.hpp>

#include "score/config/mw_someip_config_generated.h"
#include "score/result/result.h"
#include "score/socom/client_connector.hpp"

namespace score::socom {
class Runtime;
}  // namespace score::socom

namespace score::someipd {

/// \brief Represents a service offered locally (by an app behind gatewayd) on the SOME/IP network.
/// \details Owns a SOCom client connector that receives event updates from gatewayd's server
///          connector and forwards them to the SOME/IP network via vsomeip notify().
///          The service is offered on SOME/IP only while that server connector is available.
class LocalNetworkService {
   public:
    /// \brief Switches the SOME/IP offer of the service: true offers it, false withdraws it
    using Set_offered = std::function<void(bool offered)>;

    /// \brief Creates a LocalNetworkService
    /// \param service_instance_config Configuration for the service instance to create
    /// \param service_type_config Configuration for the service type of the instance to create
    /// \param vsomeip_app vsomeip application used to notify events on the SOME/IP network
    /// \param set_offered Called with true when the SOCom service becomes available, and with
    ///        false when it becomes unavailable and when this object is destroyed. MUST NOT be
    ///        empty. May be called before Create() returns, and with the same value repeatedly.
    /// \param socom_runtime SOCom runtime used to create the client connector
    /// \return Result containing the created instance on success, or an error on failure
    static Result<std::unique_ptr<LocalNetworkService>> Create(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        std::shared_ptr<vsomeip::application> vsomeip_app, Set_offered set_offered,
        socom::Runtime& socom_runtime);

    /// \brief Withdraws the SOME/IP offer
    ~LocalNetworkService();

    LocalNetworkService(const LocalNetworkService&) = delete;
    LocalNetworkService& operator=(const LocalNetworkService&) = delete;
    LocalNetworkService(LocalNetworkService&&) = delete;
    LocalNetworkService& operator=(LocalNetworkService&&) = delete;

   private:
    /// \brief Private constructor
    LocalNetworkService(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        std::shared_ptr<vsomeip::application> vsomeip_app, Set_offered set_offered,
        socom::Client_connector::Uptr client_connector);

    void forward_to_vsomeip(socom::Event_id event_id, socom::Payload payload);

    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config_;
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config_;
    std::shared_ptr<vsomeip::application> vsomeip_app_;
    Set_offered set_offered_;
    /// Declared last so it is destroyed first, ensuring no callbacks fire after the other members.
    socom::Client_connector::Uptr client_connector_;
};

}  // namespace score::someipd

#endif  // IMPL_SOMEIPD_LOCAL_NETWORK_SERVICE
