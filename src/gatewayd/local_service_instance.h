/********************************************************************************
 * Copyright (c) 2025 ETAS GmbH
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

#ifndef SRC_GATEWAYD_LOCAL_SERVICE_INSTANCE
#define SRC_GATEWAYD_LOCAL_SERVICE_INSTANCE

#include <score/mw/com/types.h>

#include <memory>
#include <vector>

#include "interfaces/someip_message_service.h"
#include "src/gatewayd/gatewayd_config_generated.h"

namespace score::someip_gateway::gatewayd {

class LocalServiceInstance {
   public:
    LocalServiceInstance(
        std::shared_ptr<const config::ServiceInstance> service_instance_config,
        score::mw::com::GenericProxy&& ipc_proxy,
        // TODO: Decouple this via an interface
        someip_message_service::SomeipMessageServiceSkeleton& someip_message_skeleton);

    static Result<mw::com::FindServiceHandle> CreateAsyncLocalService(
        std::shared_ptr<const config::ServiceInstance> service_instance_config,
        someip_message_service::SomeipMessageServiceSkeleton& someip_message_skeleton,
        std::vector<std::unique_ptr<LocalServiceInstance>>& instances);

    LocalServiceInstance(const LocalServiceInstance&) = delete;
    LocalServiceInstance& operator=(const LocalServiceInstance&) = delete;
    LocalServiceInstance(LocalServiceInstance&&) = delete;
    LocalServiceInstance& operator=(LocalServiceInstance&&) = delete;

   private:
    std::shared_ptr<const config::ServiceInstance> service_instance_config_;
    score::mw::com::GenericProxy ipc_proxy_;
    someip_message_service::SomeipMessageServiceSkeleton& someip_message_skeleton_;
};
}  // namespace score::someip_gateway::gatewayd

#endif  // SRC_GATEWAYD_LOCAL_SERVICE_INSTANCE
