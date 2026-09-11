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

#ifndef SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SOMEIPD_SERVICE_HPP
#define SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SOMEIPD_SERVICE_HPP

#include <string_view>

#include "score/mw/com/types.h"

namespace score::gateway_ipc_binding::mw_com {

/// \brief `mw::com` service interface representing the `someipd` daemon itself.
///
/// \details Unlike the bridged SOME/IP services, which are transported with `GenericSkeleton` and
///          `GenericProxy`, this interface is typed: its content is defined by this repository and
///          not by a customer's SOME/IP deployment.
///
///          It deliberately declares no service elements. Its sole purpose is peer liveness: the
///          presence of an offered instance means "the `someipd` binding is up and accepting".
///          `someipd` offers it before it sets up any bridged service, so a consumer that sees the
///          instance can rely on the peer accepting further service setup.
///
///          Service elements may be added later, for example a real `someipd` control API. Every
///          element added here needs a matching entry in the `mw_com_config.json` of both daemons.
template <typename Trait>
class Someipd_service_interface : public Trait::Base {
   public:
    using Trait::Base::Base;
};

/// \brief Consumer side of Someipd_service_interface, owned by `gatewayd`.
using Someipd_service_proxy = score::mw::com::AsProxy<Someipd_service_interface>;

/// \brief Provider side of Someipd_service_interface, owned by `someipd`.
using Someipd_service_skeleton = score::mw::com::AsSkeleton<Someipd_service_interface>;

/// \brief `serviceTypeName` that the `mw_com_config.json` of both daemons must use for
///        Someipd_service_interface.
inline constexpr std::string_view kSomeipd_service_type_name{"/score/someip/SomeipdService"};

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SOMEIPD_SERVICE_HPP
