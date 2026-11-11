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

#include "score/gateway_ipc_binding/test/unit/mw_com_mocks.hpp"

namespace score::gateway_ipc_binding::testing {

RuntimeMock::RuntimeMock() = default;
RuntimeMock::~RuntimeMock() = default;

ServiceDiscoveryMock::ServiceDiscoveryMock() = default;
ServiceDiscoveryMock::~ServiceDiscoveryMock() = default;

namespace mock_binding {

Proxy::Proxy() = default;
Proxy::~Proxy() = default;

GenericProxyEvent::GenericProxyEvent() = default;
GenericProxyEvent::~GenericProxyEvent() = default;

ProxyMethod::ProxyMethod() = default;
ProxyMethod::~ProxyMethod() = default;

Skeleton::Skeleton() = default;
Skeleton::~Skeleton() = default;

GenericSkeletonEvent::GenericSkeletonEvent() = default;
GenericSkeletonEvent::~GenericSkeletonEvent() = default;

SkeletonMethod::SkeletonMethod() = default;
SkeletonMethod::~SkeletonMethod() = default;

}  // namespace mock_binding

ProxyBindingFactoryMock::ProxyBindingFactoryMock() = default;
ProxyBindingFactoryMock::~ProxyBindingFactoryMock() = default;

GenericProxyEventBindingFactoryMock::GenericProxyEventBindingFactoryMock() = default;
GenericProxyEventBindingFactoryMock::~GenericProxyEventBindingFactoryMock() = default;

ProxyMethodBindingFactoryMock::ProxyMethodBindingFactoryMock() = default;
ProxyMethodBindingFactoryMock::~ProxyMethodBindingFactoryMock() = default;

SkeletonBindingFactoryMock::SkeletonBindingFactoryMock() = default;
SkeletonBindingFactoryMock::~SkeletonBindingFactoryMock() = default;

SkeletonEventBindingFactoryMock::SkeletonEventBindingFactoryMock() = default;
SkeletonEventBindingFactoryMock::~SkeletonEventBindingFactoryMock() = default;

SkeletonMethodBindingFactoryMock::SkeletonMethodBindingFactoryMock() = default;
SkeletonMethodBindingFactoryMock::~SkeletonMethodBindingFactoryMock() = default;

}  // namespace score::gateway_ipc_binding::testing
