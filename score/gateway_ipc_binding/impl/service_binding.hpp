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

#ifndef SCORE_GATEWAY_IPC_BINDING_IMPL_SERVICE_BINDING_HPP
#define SCORE_GATEWAY_IPC_BINDING_IMPL_SERVICE_BINDING_HPP

#include <memory>
#include <string>

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/mw/com/types.h"
#include "score/result/result.h"
#include "score/socom/runtime.hpp"

namespace score::gateway_ipc_binding::mw_com {

/// \brief One bridged SOME/IP service instance, transported over `mw::com`.
/// \details The two flavours are `Provider_service_binding` and `Consumer_service_binding`, see
///          `score/gateway_ipc_binding/docs/architecture/index.rst`, section "Role assignment".
///          Both daemons hold providers and consumers at the same time, for different services, so
///          this base class lets `Client_adapter` and `Server_adapter` store them in one vector.
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
/// \details Rejects configurations where:
///          - `instance_specifier` does not start with `"bridged_"` (case-sensitive prefix),
///          - `events` is empty,
///          - any event has an empty `name`, duplicate `name`, duplicate `id`, or
///            `max_payload_size > kMax_payload_length`,
///          - `max_sample_count == 0` when `role == Role::consumer` (`0` is allowed on
///            `Role::provider`), or `max_sample_count > kNumber_of_sample_slots` (`20`) on either
///            role.
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
/// \return Success, or Mw_com_binding_error::logic_error_invalid_instance_specifier when
///         `runtime::ResolveInstanceIDs` returns an error or an empty vector
[[nodiscard]] Result<void> check_instance_is_deployed(
    score::mw::com::InstanceSpecifier const& specifier,
    std::string const& instance_specifier) noexcept;

/// \brief Validate `services` and register any undeployed instances with the mw::com runtime.
/// \details Rejects invalid service entries or duplicate `instance_specifier` / `service_id`
///          values before modifying the runtime. Undeployed instances are registered as SHM
///          deployments (`version 1.0`, `"asil-level": "QM"`, `numberOfSampleSlots = 20`,
///          `maxSubscribers = 1`).
/// \retval void All services are valid and deployed.
/// \retval Mw_com_binding_error::logic_error_invalid_service_configuration A service entry is
///         invalid or `services` contains duplicate `instance_specifier` or `service_id` values.
/// \retval Mw_com_binding_error::logic_error_invalid_instance_specifier An `instance_specifier`
///         cannot be parsed by `mw::com::InstanceSpecifier`.
/// \retval Mw_com_binding_error::runtime_error_service_setup_failed Extending the mw::com runtime
///         configuration failed.
[[nodiscard]] Result<void> add_to_mw_com_runtime(Service_configs const& services) noexcept;

/// \brief Create the binding half this peer implements for one bridged service instance.
/// \details Sets up the provider or consumer endpoint eagerly according to `config.role`; consumer
///          service discovery starts later on `Service_binding::start()`.
/// \param runtime SOCom runtime used to create the connector.
/// \param config Bridged service instance to set up.
/// \return Created binding, or the validation / setup error encountered.
[[nodiscard]] Result<std::unique_ptr<Service_binding>> make_service_binding(
    score::socom::Runtime& runtime, Service_config const& config) noexcept;

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_IMPL_SERVICE_BINDING_HPP
