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

#ifndef IMPL_SOMEIPD_ROUTING
#define IMPL_SOMEIPD_ROUTING

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#if !defined(SOMEIPD_STACK_OPENSOMEIP)
#include <thread>
#include <vsomeip/vsomeip.hpp>
#endif

#include "score/config/mw_someip_config_generated.h"
#include "score/result/result.h"

namespace score::someipd {

#if defined(SOMEIPD_STACK_OPENSOMEIP)
class OpensomeipHost;
#endif

/// Manages the SOME/IP stack lifecycle and service registrations.
///
/// The stack is selected at build time (`--//score/someipd:stack=vsomeip|opensomeip`).
/// vsomeip is the default. opensomeip is composed in-process: someipd creates the sockets.
/// IPC forwarding to and from gatewayd is handled via SOCom connectors that are wired in
/// externally (see GatewaydServices).
class Routing {
   public:
    /// @brief Creates a Routing instance.
    ///
    /// Nothing is offered until AddServices() is called and the stack is running.
    ///
    /// @return A Routing instance, or an error if the stack could not be created.
    static Result<std::unique_ptr<Routing>> Create();

    ~Routing();

    Routing(const Routing&) = delete;
    Routing& operator=(const Routing&) = delete;
    Routing(Routing&&) = delete;
    Routing& operator=(Routing&&) = delete;

#if defined(SOMEIPD_STACK_OPENSOMEIP)
    /// \brief In-process opensomeip host. Valid after Create().
    OpensomeipHost& host() noexcept;
#else
    /// Returns the vsomeip application instance.
    std::shared_ptr<vsomeip::application> get_application() const noexcept { return application_; }
#endif

    /// \brief Offers the local service instances of @p config, then calls @p on_registered.
    /// \details Both run now if the stack is up, and again when it becomes registered.
    ///          Thread safe.
    /// \param on_registered MUST NOT be empty. Use it to call Setup() on RemoteNetworkService.
    void AddServices(std::shared_ptr<const score::mw_someip_config::Root> config,
                     std::function<void()> on_registered);

    /// Starts the network stack, blocks in @p wait_for_shutdown, then stops the stack.
    /// \param wait_for_shutdown Blocks until shutdown is requested.
    void Run(const std::function<void()>& wait_for_shutdown);

   private:
    struct Services {
        std::shared_ptr<const score::mw_someip_config::Root> config;
        std::function<void()> on_registered;
    };

    Routing() = default;
    void SetupOfferings(score::mw_someip_config::Root const& config);

#if defined(SOMEIPD_STACK_OPENSOMEIP)
    std::unique_ptr<OpensomeipHost> host_{};
#else
    std::shared_ptr<vsomeip::application> application_{};
    std::shared_ptr<vsomeip::payload> payload_{};
    std::thread processing_thread_{};
#endif
    /// Guards registered_ and services_. AddServices() and the stack's registered callback use
    /// them.
    std::mutex mutex_{};
    bool registered_{false};
    std::vector<Services> services_{};
};

}  // namespace score::someipd

#endif  // IMPL_SOMEIPD_ROUTING
