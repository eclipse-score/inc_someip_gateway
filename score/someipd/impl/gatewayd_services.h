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

#ifndef IMPL_SOMEIPD_GATEWAYD_SERVICES
#define IMPL_SOMEIPD_GATEWAYD_SERVICES

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/socom/runtime.hpp"
#include "score/someipd/impl/local_network_service.h"
#include "score/someipd/impl/remote_network_service.h"
#include "score/someipd/impl/routing.h"
#include "score/span.hpp"

namespace score::someipd {

/// \brief The services that gatewayds added to someipd
/// \details someipd keeps them until it stops.
class GatewaydServices {
   public:
    GatewaydServices(Routing& routing, socom::Runtime& socom_runtime) noexcept;

    /// \brief Sets up the services of one gatewayd, or none of them
    /// \details Calls MUST NOT overlap. MUST NOT call anything that waits for an mw::com reply.
    /// \param gateway_config mw_someip_config flatbuffer of the gatewayd
    /// \param add_bridged_services Sets up the bridged services of the gatewayd
    /// \return False if `gateway_config` is invalid, has a service of an earlier call, or a
    ///         service can't be set up
    bool Add(score::cpp::span<std::byte const> gateway_config,
             gateway_ipc_binding::mw_com::Add_bridged_services const& add_bridged_services);

   private:
    struct NetworkServices {
        std::vector<std::unique_ptr<LocalNetworkService>> local;
        std::vector<std::unique_ptr<RemoteNetworkService>> remote;
    };

    Routing& routing_;
    socom::Runtime& socom_runtime_;
    std::set<std::uint16_t> service_ids_;
    std::vector<NetworkServices> network_services_;
};

}  // namespace score::someipd

#endif  // IMPL_SOMEIPD_GATEWAYD_SERVICES
