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
///          `docs/mw_com_binding.rst`, section "Role assignment". Both daemons hold providers and
///          consumers at the same time, for different services, so this base class lets
///          `Client_adapter` and `Server_adapter` store them in one vector.
class Service_binding {
   public:
    Service_binding() = default;
    virtual ~Service_binding() noexcept = default;

    Service_binding(Service_binding const&) = delete;
    Service_binding& operator=(Service_binding const&) = delete;
    Service_binding(Service_binding&&) = delete;
    Service_binding& operator=(Service_binding&&) = delete;

    /// \brief Start the mw::com service discovery of a consumer. A provider has none.
    /// \details MUST be called once. MUST NOT run on an mw::com message passing thread.
    [[nodiscard]] virtual Result<void> start() noexcept = 0;
};

/// \brief Validate a bridged service configuration before setting it up.
/// \details Rejects configurations with no events, empty or duplicate event names, duplicate
///          event IDs, payloads above `kMax_payload_length`, `max_sample_count == 0` on a
///          consumer, or `max_sample_count > kNumber_of_sample_slots`.
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

/// \brief Validate `services` and register any undeployed instances with the mw::com runtime.
/// \details Also rejects duplicate `instance_specifier` or `service_id` values across `services`.
/// \return Success, or the first error encountered before modifying the mw::com configuration
[[nodiscard]] Result<void> add_to_mw_com_runtime(Service_configs const& services) noexcept;

/// \brief Create the binding half this peer implements for one bridged service instance.
/// \details Dispatches on `Service_config::role`, see `docs/mw_com_binding.rst`. Setup is eager:
///          a provider creates the `GenericSkeleton` and SOCom client connector, and a consumer
///          creates the SOCom server connector, so setup errors are reported immediately.
///          Consumer service discovery waits for `Service_binding::start()`.
/// \param runtime SOCom runtime used to create the connector
/// \param config Bridged service instance to set up
/// \return The service binding, or the first error encountered
[[nodiscard]] Result<std::unique_ptr<Service_binding>> make_service_binding(
    score::socom::Runtime& runtime, Service_config const& config) noexcept;

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SERVICE_BINDING_HPP
