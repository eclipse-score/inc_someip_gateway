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

#ifndef SRC_GATEWAY_IPC_BINDING_SRC_REQUEST_SERVICE_HANDLE
#define SRC_GATEWAY_IPC_BINDING_SRC_REQUEST_SERVICE_HANDLE

#include <score/os/errno.h>

#include <cassert>
#include <score/expected.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <score/socom/runtime.hpp>

namespace score::gateway_ipc_binding {

class Service_request_sender {
   public:
    Service_request_sender() = default;
    virtual ~Service_request_sender() = default;

    virtual void send_request_service(
        score::socom::Service_interface_definition const& configuration,
        score::socom::Service_instance const& instance, bool in_use) noexcept = 0;
};

class Request_service_handle final : public score::socom::Service_request_handle {
   public:
    Request_service_handle(Service_request_sender& owner,
                           score::socom::Service_interface_definition configuration,
                           score::socom::Service_instance instance)
        : m_owner(owner),
          m_configuration(std::move(configuration)),
          m_instance(std::move(instance)) {
        m_owner.send_request_service(m_configuration, m_instance, true);
    }

    ~Request_service_handle() override {
        m_owner.send_request_service(m_configuration, m_instance, false);
    }

   private:
    Service_request_sender& m_owner;
    score::socom::Service_interface_definition m_configuration;
    score::socom::Service_instance m_instance;
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_SRC_REQUEST_SERVICE_HANDLE
