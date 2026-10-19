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

#include <cstdint>
#include <string_view>

#include "score/gateway_ipc_binding/fixed_size_container.hpp"
#include "score/mw/com/types.h"

namespace score::gateway_ipc_binding::mw_com {

/// \brief Payload size of one bridged event.
struct Bridged_event_size {
    std::uint16_t service_id;
    std::uint16_t event_id;
    std::uint32_t max_payload_size;
};

/// \brief SetBridgedEventSizes argument, one entry per consumer event of gatewayd.
using Bridged_event_sizes = Fixed_size_container<Bridged_event_size, 1024U>;

/// \brief `mw::com` service interface representing the `someipd` daemon itself.
///
/// \details Unlike the bridged SOME/IP services, which are transported with `GenericSkeleton` and
///          `GenericProxy`, this interface is typed: its content is defined by this repository and
///          not by a customer's SOME/IP deployment.
///
///          The presence of an offered instance is the peer-liveness signal. Its
///          SetBridgedEventSizes method tells someipd the sizes of the events it provides. Every
///          element declared here MUST have a matching entry in the `mw_com_config.json` of
///          both daemons.
template <typename Trait>
class Someipd_service_interface : public Trait::Base {
   public:
    using Trait::Base::Base;

    typename Trait::template Method<bool(Bridged_event_sizes)> set_bridged_event_sizes{
        *this, "SetBridgedEventSizes"};
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
