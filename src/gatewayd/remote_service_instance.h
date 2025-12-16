/********************************************************************************
 * Copyright (c) 2025 Contributors to the Eclipse Foundation
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

#ifndef SRC_GATEWAYD_REMOTE_SERVICE_INSTANCE
#define SRC_GATEWAYD_REMOTE_SERVICE_INSTANCE

#include <memory>
#include <vector>

#include "score/mw/com/types.h"
#include "src/gatewayd/gatewayd_config_generated.h"
#include "src/network_service/interfaces/message_transfer.h"
#include "tests/performance_benchmarks/echo_service.h"

namespace score::someip_gateway::gatewayd {

class RemoteServiceInstance {
   public:
    RemoteServiceInstance(std::shared_ptr<const config::ServiceInstance> service_instance_config,
                          // TODO: Use something generic (template)?
                          echo_service::EchoResponseSkeleton&& ipc_skeleton,
                          network_service::interfaces::message_transfer::SomeipMessageTransferProxy
                              someip_message_proxy);

    static Result<mw::com::FindServiceHandle> CreateAsyncRemoteService(
        std::shared_ptr<const config::ServiceInstance> service_instance_config,
        std::vector<std::unique_ptr<RemoteServiceInstance>>& instances);

    RemoteServiceInstance(const RemoteServiceInstance&) = delete;
    RemoteServiceInstance& operator=(const RemoteServiceInstance&) = delete;
    RemoteServiceInstance(RemoteServiceInstance&&) = delete;
    RemoteServiceInstance& operator=(RemoteServiceInstance&&) = delete;

   private:
    std::shared_ptr<const config::ServiceInstance> service_instance_config_;
    echo_service::EchoResponseSkeleton ipc_skeleton_;
    network_service::interfaces::message_transfer::SomeipMessageTransferProxy someip_message_proxy_;
};
}  // namespace score::someip_gateway::gatewayd

#endif  // SRC_GATEWAYD_REMOTE_SERVICE_INSTANCE
