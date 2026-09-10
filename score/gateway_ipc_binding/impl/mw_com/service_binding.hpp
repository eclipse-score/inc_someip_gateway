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

#ifndef SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SERVICE_BINDING_HPP
#define SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SERVICE_BINDING_HPP

#include <memory>
#include <string>

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/mw/com/types.h"
#include "score/result/result.h"
#include "score/socom/runtime.hpp"

namespace score::gateway_ipc_binding::mw_com {

/// \brief One bridged SOME/IP service instance, transported over `mw::com`.
/// \details The two flavours are `Provider_service_binding` and `Consumer_service_binding`, see
///          `docs/mw_com_binding.rst`, section "Role assignment". Both daemons hold both roles at
///          the same time, for different services, so this base exists only so that
///          `Mw_com_binding` can own a homogeneous list of them.
class Service_binding {
   public:
    Service_binding() = default;
    virtual ~Service_binding() noexcept = default;

    Service_binding(Service_binding const&) = delete;
    Service_binding& operator=(Service_binding const&) = delete;
    Service_binding(Service_binding&&) = delete;
    Service_binding& operator=(Service_binding&&) = delete;
};

/// \brief Reject a service configuration that cannot possibly work before anything is created.
/// \details Catches the mistakes that would otherwise only surface as a silently dead bridge:
///          a missing or duplicated event name, a payload longer than the length prefix can
///          express, and a consumer that would subscribe for zero samples.
/// \param config Bridged service instance to check
/// \return Success, or Mw_com_binding_error::logic_error_invalid_service_configuration
[[nodiscard]] Result<void> validate(Service_config const& config) noexcept;

/// \brief Turn a configured string into a mw::com InstanceSpecifier.
/// \param instance_specifier Value of Service_config::instance_specifier
/// \return The specifier, or Mw_com_binding_error::logic_error_invalid_instance_specifier
[[nodiscard]] Result<score::mw::com::InstanceSpecifier> make_instance_specifier(
    std::string const& instance_specifier) noexcept;

/// \brief Check that the mw::com deployment actually knows the instance.
/// \details `StartFindService()` accepts a well-formed specifier that resolves to no instance at
///          all and then simply never reports anything. Resolving up front turns that silent
///          misconfiguration into an error at construction time, symmetric to the provider side,
///          where `Create()` already fails for an unknown instance.
/// \param specifier Specifier to resolve
/// \param instance_specifier Its configured string form, for the error message
/// \return Success, or Mw_com_binding_error::logic_error_invalid_instance_specifier
[[nodiscard]] Result<void> check_instance_is_deployed(
    score::mw::com::InstanceSpecifier const& specifier,
    std::string const& instance_specifier) noexcept;

/// \brief Create the binding half this peer implements for one bridged service instance.
/// \details Dispatches on `Service_config::role`, see `docs/mw_com_binding.rst`. Setup is eager:
///          the `GenericSkeleton` respectively the service discovery and the SOCom connector are
///          created here, so a configuration error is reported to the caller instead of leaving a
///          bridge that never carries data.
/// \param runtime SOCom runtime used to create the connector
/// \param config Bridged service instance to set up
/// \return The service binding, or the first error encountered
[[nodiscard]] Result<std::unique_ptr<Service_binding>> make_service_binding(
    score::socom::Runtime& runtime, Service_config const& config) noexcept;

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SERVICE_BINDING_HPP
