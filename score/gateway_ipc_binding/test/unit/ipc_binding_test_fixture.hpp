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

#ifndef SCORE_GATEWAY_IPC_BINDING_TEST_UNIT_IPC_BINDING_TEST_FIXTURE_HPP
#define SCORE_GATEWAY_IPC_BINDING_TEST_UNIT_IPC_BINDING_TEST_FIXTURE_HPP

#include <gmock/gmock.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/gateway_ipc_binding/test/unit/mw_com_mocks.hpp"
#include "score/mw/com/impl/handle_type.h"
#include "score/mw/com/impl/instance_identifier.h"
#include "score/socom/runtime_mock.hpp"
#include "score/socom/service_interface_identifier.hpp"

namespace score::gateway_ipc_binding::mw_com {

inline constexpr std::string_view kDefaultServiceInterfacePath{"/score/test/Service"};
inline constexpr std::string_view kDefaultServiceTypeName{"test_type"};
inline constexpr std::string_view kDefaultServiceInstanceName{"1"};
inline constexpr std::uint16_t kDefaultServiceId{100U};
inline constexpr std::uint16_t kDefaultInstanceId{1U};
inline constexpr std::uint8_t kDefaultMajorVersion{1U};
inline constexpr std::uint16_t kDefaultMinorVersion{0U};
inline constexpr std::uint16_t kEvent0Id{1U};

inline constexpr std::string_view kValidBridgedSpecifier{"bridged_ipc/service"};
inline constexpr std::string_view kSomeipdSpecifier{"someipd/daemon"};
inline constexpr std::string_view kInvalidSpecifier{"invalid//specifier"};
inline constexpr std::string_view kEvent0{"ev0"};
inline constexpr std::size_t kEvent0MaxPayload{16U};
inline constexpr std::size_t kDefaultMaxSampleCount{4U};

inline Service_config MakeServiceConfig(Role const role = Role::consumer,
                                        std::string_view const specifier = kValidBridgedSpecifier) {
    socom::Service_interface_identifier const interface{
        kDefaultServiceInterfacePath,
        socom::Literal_tag{},
        {kDefaultMajorVersion, kDefaultMinorVersion}};
    socom::Service_instance const instance{kDefaultServiceInstanceName, socom::Literal_tag{}};
    return Service_config{
        interface,
        instance,
        std::string{specifier},
        kDefaultServiceId,
        kDefaultInstanceId,
        role,
        {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, kEvent0MaxPayload}},
        kDefaultMaxSampleCount,
    };
}

class IpcBindingTestFixture : public testing::MwComTestFixture {
   protected:
    IpcBindingTestFixture() { StubInstanceResolved(); }

    using MwComTestFixture::StubInstanceResolved;

    void StubInstanceResolved(std::initializer_list<std::string_view> const events = {kEvent0}) {
        MwComTestFixture::StubInstanceResolved(
            kValidBridgedSpecifier, events,
            testing::ServiceDeploymentSpec{kDefaultServiceTypeName, kDefaultServiceId,
                                           kDefaultMajorVersion, kDefaultMinorVersion,
                                           kDefaultInstanceId});
    }

    score::mw::com::impl::InstanceIdentifier MakeIdentifier(
        std::initializer_list<std::string_view> const events = {kEvent0}) {
        return instance_holder().MakeIdentifier(
            kValidBridgedSpecifier, events,
            testing::ServiceDeploymentSpec{kDefaultServiceTypeName, kDefaultServiceId,
                                           kDefaultMajorVersion, kDefaultMinorVersion,
                                           kDefaultInstanceId});
    }

    score::mw::com::impl::HandleType MakeHandle(
        std::initializer_list<std::string_view> const events = {kEvent0}) {
        return instance_holder().MakeHandle(
            kValidBridgedSpecifier, events,
            testing::ServiceDeploymentSpec{kDefaultServiceTypeName, kDefaultServiceId,
                                           kDefaultMajorVersion, kDefaultMinorVersion,
                                           kDefaultInstanceId});
    }

    ::testing::StrictMock<socom::Runtime_mock> socom_runtime_{};
};

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_TEST_UNIT_IPC_BINDING_TEST_FIXTURE_HPP
