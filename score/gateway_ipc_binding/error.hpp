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

#ifndef SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_ERROR
#define SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_ERROR

#include <string_view>

#include "score/result/error.h"
#include "score/result/error_code.h"

namespace score::gateway_ipc_binding {

/// \brief Error conditions of the mw::com binding implementation
/// \see score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp
enum class Mw_com_binding_error : score::result::ErrorCode {
    /// The configured string is not a valid mw::com InstanceSpecifier
    logic_error_invalid_instance_specifier,
    /// The SomeipdService skeleton could not be created, e.g. it is missing from mw_com_config.json
    runtime_error_someipd_service_creation_failed,
    /// OfferService() on the SomeipdService skeleton failed
    runtime_error_someipd_service_offer_failed,
    /// StartFindService() for the SomeipdService instance failed
    runtime_error_someipd_service_find_failed,
    /// The SomeipdService proxy could not be created from a discovered handle
    runtime_error_someipd_service_proxy_creation_failed,
    /// start() was called more than once
    logic_error_already_started,
    /// A Service_config cannot describe a working bridge, e.g. it has duplicate event names
    logic_error_invalid_service_configuration,
    /// An event id has no counterpart in the Service_config of the bridged service
    logic_error_unknown_event,
    /// A bridged service could not be set up, e.g. it is missing from mw_com_config.json
    runtime_error_service_setup_failed,
    /// StartFindService() for a bridged service instance failed
    runtime_error_service_find_failed,
    /// The sample size mw::com laid out differs from the one derived from the Service_config
    runtime_error_sample_size_mismatch,
    /// GenericSkeletonEvent::Allocate() failed, i.e. all sample slots are in use
    runtime_error_sample_allocation_failed,
};

score::result::Error MakeError(Mw_com_binding_error code,
                               std::string_view user_message = "") noexcept;

}  // namespace score::gateway_ipc_binding

#endif  // SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_ERROR
