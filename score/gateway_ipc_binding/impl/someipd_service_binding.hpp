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

#ifndef SCORE_GATEWAY_IPC_BINDING_IMPL_SOMEIPD_SERVICE_BINDING_HPP
#define SCORE_GATEWAY_IPC_BINDING_IMPL_SOMEIPD_SERVICE_BINDING_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "score/result/result.h"
#include "someipd_service.hpp"

namespace score::gateway_ipc_binding::mw_com {

/// \brief Provider half of the SomeipdService peer-liveness contract, owned by `someipd`.
/// \details Owns the `Someipd_service_skeleton`. Its offered instance is what the consumer half
///          observes. It also handles AddServiceConfiguration.
class Someipd_service_provider {
   public:
    using Add_service_configuration_handler = std::function<bool(Service_configuration const&)>;

    /// \brief Creates the SomeipdService skeleton without offering it yet.
    /// \details Destroying the provider waits for any running handler to complete.
    /// \pre `add_service_configuration_handler` is non-empty.
    /// \param instance_specifier mw::com InstanceSpecifier of the SomeipdService instance.
    /// \param add_service_configuration_handler Callback invoked on AddServiceConfiguration calls.
    /// \retval std::unique_ptr<Someipd_service_provider> Created provider.
    /// \retval Mw_com_binding_error::logic_error_invalid_service_configuration Handler is empty.
    /// \retval Mw_com_binding_error::logic_error_invalid_instance_specifier Specifier is invalid.
    /// \retval Mw_com_binding_error::runtime_error_someipd_service_creation_failed Skeleton
    ///         creation or method handler registration failed.
    static Result<std::unique_ptr<Someipd_service_provider>> create(
        std::string const& instance_specifier,
        Add_service_configuration_handler add_service_configuration_handler) noexcept;

    /// \brief Withdraws the service offer (`StopOfferService()`) iff `offer()` succeeded.
    ~Someipd_service_provider() noexcept;

    Someipd_service_provider(Someipd_service_provider const&) = delete;
    Someipd_service_provider& operator=(Someipd_service_provider const&) = delete;
    Someipd_service_provider(Someipd_service_provider&&) = delete;
    Someipd_service_provider& operator=(Someipd_service_provider&&) = delete;

    /// \brief Offers the SomeipdService instance over mw::com.
    /// \retval void Service offered.
    /// \retval Mw_com_binding_error::runtime_error_someipd_service_offer_failed OfferService()
    ///         failed.
    Result<void> offer() noexcept;

   private:
    explicit Someipd_service_provider(Someipd_service_skeleton skeleton) noexcept;

    Someipd_service_skeleton m_skeleton;
};

/// \brief Consumer half of the SomeipdService peer-liveness contract, owned by `gatewayd`.
/// \details Runs `StartFindService` for the peer's `SomeipdService` instance:
///          - On non-empty `handles`, creates a `Someipd_service_proxy` for `handles.front()` and
///            calls `add_service_configuration(service_configuration)`. `is_connected()` becomes
///            `true` iff the call succeeds and returns `true`; if proxy creation fails or
///            `add_service_configuration` fails or returns `false`, `is_connected()` is `false` and
///            no proxy is retained.
///          - Duplicate discovery notifications for the already-connected handle (`handles.front()
///            == active_handle`) are ignored, keeping `is_connected() == true` without dropping
///            connectivity or recreating the proxy; a distinct handle replaces the connection.
///          - Empty `handles` sets `is_connected()` to `false` and destroys any active proxy.
class Someipd_service_consumer {
   public:
    /// \brief Starts service discovery for the peer's SomeipdService instance.
    /// \details Discovery runs until destruction; `is_connected()` may already be true on return.
    /// \param instance_specifier mw::com InstanceSpecifier of the SomeipdService instance.
    /// \param service_configuration Sent via AddServiceConfiguration whenever a proxy connects.
    /// \retval std::unique_ptr<Someipd_service_consumer> Created consumer.
    /// \retval Mw_com_binding_error::logic_error_invalid_instance_specifier Specifier is invalid
    ///         or undeployed.
    /// \retval Mw_com_binding_error::runtime_error_someipd_service_find_failed Starting discovery
    ///         failed.
    static Result<std::unique_ptr<Someipd_service_consumer>> create(
        std::string const& instance_specifier,
        Service_configuration const& service_configuration) noexcept;

    ~Someipd_service_consumer() noexcept;

    Someipd_service_consumer(Someipd_service_consumer const&) = delete;
    Someipd_service_consumer& operator=(Someipd_service_consumer const&) = delete;
    Someipd_service_consumer(Someipd_service_consumer&&) = delete;
    Someipd_service_consumer& operator=(Someipd_service_consumer&&) = delete;

    /// \brief True while the peer's SomeipdService instance is available and has accepted the
    ///        service configuration
    /// \details Read from a different thread than the find-service handler that maintains it.
    [[nodiscard]] bool is_connected() const noexcept;

   private:
    explicit Someipd_service_consumer(
        std::unique_ptr<Service_configuration const> service_configuration) noexcept;

    void on_find_service(
        score::mw::com::ServiceHandleContainer<score::mw::com::HandleType> handles) noexcept;

    /// \brief Guards discovery state and m_proxy against concurrent handler calls and teardown
    std::mutex m_mutex;
    bool m_stopping{false};
    std::uint64_t m_discovery_epoch{0U};
    std::optional<score::mw::com::FindServiceHandle> m_find_handle{};
    std::optional<score::mw::com::HandleType> m_active_handle{};
    std::optional<Someipd_service_proxy> m_proxy{};
    std::atomic<bool> m_connected{false};
    std::unique_ptr<Service_configuration const> m_service_configuration;
};

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_IMPL_SOMEIPD_SERVICE_BINDING_HPP
