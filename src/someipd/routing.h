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

#ifndef SRC_SOMEIPD_ROUTING
#define SRC_SOMEIPD_ROUTING

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vsomeip/vsomeip.hpp>

#include "score/result/result.h"
#include "score/someip/types.h"
#include "src/config/mw_someip_config_generated.h"

namespace score::someipd {

using score::someip::EventId;
using score::someip::InstanceId;
using score::someip::ServiceId;

/// Manages the vsomeip application lifecycle and SOME/IP service registrations.
///
/// Owns a vsomeip application instance, registers event subscriptions and
/// service offerings from configuration, and dispatches incoming SOME/IP
/// messages. IPC forwarding to/from gatewayd is handled via SOCom connectors
/// that are wired in externally (see main.cpp).
class Routing {
   public:
    /// @brief Creates a Routing instance from the given configuration.
    ///
    /// Initialises a vsomeip application and registers it with the vsomeip runtime.
    /// Subscriptions and service offerings are applied once Run() is called and the
    /// application has registered with the vsomeip routing daemon.
    ///
    /// @param config SOME/IP gateway configuration describing the services, instances,
    ///               events, and methods to subscribe to or offer on the network.
    /// @return A fully initialised Routing instance, or an error if the vsomeip application
    ///         could not be created or initialised.
    static Result<Routing> Create(std::shared_ptr<const score::mw_someip_config::Root> config);

    ~Routing() = default;

    Routing(const Routing&) = delete;
    Routing& operator=(const Routing&) = delete;
    Routing(Routing&&) noexcept;
    Routing& operator=(Routing&&) noexcept;

    /// Returns the vsomeip application instance.
    std::shared_ptr<vsomeip::application> get_application() const noexcept { return application_; }

    /// Runs the routing loop, blocking until @p shutdown_requested is set to true.
    /// \param on_registered Optional callback invoked once vsomeip reaches ST_REGISTERED.
    ///        Use this to call setup_vsomeip() on RemoteNetworkService instances.
    void Run(std::atomic<bool>& shutdown_requested, std::function<void()> on_registered = {});

   private:
    explicit Routing(std::shared_ptr<const score::mw_someip_config::Root> config);
    void SetupOfferings();
    void ProcessMessages(std::atomic<bool>& shutdown_requested);
    InstanceId LookupInstanceId(ServiceId service_id) const;

    std::shared_ptr<const score::mw_someip_config::Root> config_;
    std::shared_ptr<vsomeip::application> application_{};
    std::shared_ptr<vsomeip::payload> payload_{};
    std::thread processing_thread_{};
};

}  // namespace score::someipd

#endif  // SRC_SOMEIPD_ROUTING
