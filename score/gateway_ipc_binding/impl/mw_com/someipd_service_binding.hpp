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

#ifndef SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SOMEIPD_SERVICE_BINDING_HPP
#define SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SOMEIPD_SERVICE_BINDING_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "score/result/result.h"
#include "score/someip/someipd_service.hpp"

namespace score::gateway_ipc_binding::mw_com {

/// \brief Provider half of the SomeipdService peer-liveness contract, owned by `someipd`.
/// \details Owns the `Someipd_service_skeleton`. Its offered instance is what the consumer half
///          observes. It carries no data; being offered *is* the signal.
class Someipd_service_provider {
   public:
    /// \brief Create the skeleton for the given instance specifier without offering it yet
    /// \param instance_specifier mw::com InstanceSpecifier of the SomeipdService instance
    /// \return The provider, or an error if the specifier is invalid or the skeleton could not be
    ///         created, e.g. because the instance is missing from mw_com_config.json
    static Result<std::unique_ptr<Someipd_service_provider>> create(
        std::string const& instance_specifier) noexcept;

    ~Someipd_service_provider() noexcept;

    Someipd_service_provider(Someipd_service_provider const&) = delete;
    Someipd_service_provider& operator=(Someipd_service_provider const&) = delete;
    Someipd_service_provider(Someipd_service_provider&&) = delete;
    Someipd_service_provider& operator=(Someipd_service_provider&&) = delete;

    /// \brief Offer the instance, making the peer's is_connected() go true
    /// \return Success, or an error if OfferService() failed
    Result<void> offer() noexcept;

   private:
    explicit Someipd_service_provider(someip::Someipd_service_skeleton skeleton) noexcept;

    someip::Someipd_service_skeleton m_skeleton;
};

/// \brief Consumer half of the SomeipdService peer-liveness contract, owned by `gatewayd`.
/// \details Runs `StartFindService` for the instance the peer provides and keeps a proxy for it
///          while it is available. The existence of that proxy is what `is_connected()` reports.
class Someipd_service_consumer {
   public:
    /// \brief Start service discovery for the given instance specifier
    /// \details Discovery runs until this object is destroyed. The find-service handler may already
    ///          fire before this function returns, so is_connected() can be true immediately.
    /// \param instance_specifier mw::com InstanceSpecifier of the SomeipdService instance
    /// \return The consumer, or an error if the specifier is invalid or discovery failed to start
    static Result<std::unique_ptr<Someipd_service_consumer>> create(
        std::string const& instance_specifier) noexcept;

    ~Someipd_service_consumer() noexcept;

    Someipd_service_consumer(Someipd_service_consumer const&) = delete;
    Someipd_service_consumer& operator=(Someipd_service_consumer const&) = delete;
    Someipd_service_consumer(Someipd_service_consumer&&) = delete;
    Someipd_service_consumer& operator=(Someipd_service_consumer&&) = delete;

    /// \brief True while the peer's SomeipdService instance is available
    /// \details Read from a different thread than the find-service handler that maintains it.
    [[nodiscard]] bool is_connected() const noexcept;

   private:
    Someipd_service_consumer() noexcept = default;

    void on_find_service(
        score::mw::com::ServiceHandleContainer<score::mw::com::HandleType> handles) noexcept;

    /// \brief Guards m_proxy against concurrent find-service handler invocations and teardown
    std::mutex m_mutex;
    std::optional<someip::Someipd_service_proxy> m_proxy;
    std::atomic<bool> m_connected{false};
    std::optional<score::mw::com::FindServiceHandle> m_find_handle;
};

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SOMEIPD_SERVICE_BINDING_HPP
