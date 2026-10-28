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
#include <thread>
#include <vector>
#include <vsomeip/vsomeip.hpp>

#include "score/config/mw_someip_config_generated.h"
#include "score/result/result.h"

namespace score::someipd {

/// Manages the vsomeip application lifecycle and SOME/IP service registrations.
///
/// Owns a vsomeip application instance, offers the services that gatewayds add at runtime,
/// and dispatches incoming SOME/IP messages. IPC forwarding to/from gatewayd is handled via
/// SOCom connectors that are wired in externally (see GatewaydServices).
class Routing {
   public:
    /// @brief Creates a Routing instance.
    ///
    /// Initialises a vsomeip application and registers it with the vsomeip runtime.
    /// Nothing is offered until AddServices() is called and the application has registered
    /// with the vsomeip routing daemon.
    ///
    /// @return A fully initialised Routing instance, or an error if the vsomeip application
    ///         could not be created or initialised.
    static Result<std::unique_ptr<Routing>> Create();

    ~Routing() = default;

    Routing(const Routing&) = delete;
    Routing& operator=(const Routing&) = delete;
    Routing(Routing&&) = delete;
    Routing& operator=(Routing&&) = delete;

    /// Returns the vsomeip application instance.
    std::shared_ptr<vsomeip::application> get_application() const noexcept { return application_; }

    /// \brief Offers the local service instances of @p config, then calls @p on_registered
    /// \details Both run now if vsomeip is registered, and again on every ST_REGISTERED.
    ///          Thread safe.
    /// \param on_registered MUST NOT be empty. Use it to call setup_vsomeip() on
    ///        RemoteNetworkService instances.
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

    std::shared_ptr<vsomeip::application> application_{};
    std::shared_ptr<vsomeip::payload> payload_{};
    std::thread processing_thread_{};
    /// Guards registered_ and services_. The vsomeip state handler and AddServices() use them.
    std::mutex mutex_{};
    bool registered_{false};
    std::vector<Services> services_{};
};

}  // namespace score::someipd

#endif  // IMPL_SOMEIPD_ROUTING
