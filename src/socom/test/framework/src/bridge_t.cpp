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

#include "bridge_t.hpp"

#include "score/socom/runtime.hpp"

using ::score::socom::Bridge_identity;
using ::score::socom::Construction_error;
using ::score::socom::Find_result_status;
using ::score::socom::Find_subscription_handle;
using ::score::socom::Service_bridge_registration;
using ::score::socom::Service_instance;
using ::score::socom::Service_interface;
using ::score::socom::Service_interface_configuration;
using ::score::socom::Service_request_handle;
using ::testing::_;
using ::testing::Assign;
using ::testing::ByMove;
using ::testing::Return;
using ::testing::SaveArg;

namespace ac {

namespace {

bool enable_subscribe_find_service(Bridge_data::Expect const& expect) {
    return (Bridge_data::Expect::both == expect) ||
           (Bridge_data::Expect::subscribe_find_service == expect);
}

bool enable_request_find_service(Bridge_data::Expect const& expect) {
    return (Bridge_data::Expect::both == expect) ||
           (Bridge_data::Expect::request_service_function == expect);
}

}  // namespace

class Handle_mock final : public Service_request_handle, public Find_subscription_handle {
    std::atomic<bool>& m_destroyed;

   public:
    explicit Handle_mock(std::atomic<bool>& destroyed) : m_destroyed{destroyed} {}
    Handle_mock(Handle_mock const&) = delete;
    Handle_mock(Handle_mock&&) = delete;
    ~Handle_mock() override { m_destroyed = true; }
    Handle_mock& operator=(Handle_mock const&) = delete;
    Handle_mock& operator=(Handle_mock&&) = delete;
};

score::Result<Service_bridge_registration> Bridge_data::register_at_runtime(
    Connector_factory& connector_factory) {
    auto registration = connector_factory.register_service_bridge(
        m_identity, m_sfsf_mock.AsStdFunction(), m_rsf_mock.AsStdFunction());
    return registration;
}

void Bridge_data::expect_subscribe_find_service(
    Service_interface const& interface, std::optional<Service_instance> instance,
    Subscribe_find_service_function const& sfs_callback) {
    EXPECT_CALL(m_sfsf_mock, Call(_, interface, instance))
        .WillOnce(DoAll(
            SaveArg<0>(&m_find_result_callback), Assign(&m_subscribe_find_service_created, true),
            Assign(&m_subscribe_find_service_destroyed, false), sfs_callback,
            Return(ByMove(std::make_unique<Handle_mock>(m_subscribe_find_service_destroyed)))));
}

void Bridge_data::expect_another_subscribe_find_service(
    Service_interface const& interface, std::optional<Service_instance> instance,
    Subscribe_find_service_function const& sfs_callback) {
    EXPECT_CALL(m_sfsf_mock, Call(_, interface, instance)).WillOnce(sfs_callback);
}

void Bridge_data::expect_request_find_service(
    Service_interface_configuration const& configuration, Service_instance const& instance,
    std::function<void(Service_interface_configuration const&, Service_instance const&)>&& rsf) {
    EXPECT_CALL(m_rsf_mock, Call(configuration, instance))
        .WillOnce(
            DoAll(Assign(&m_request_find_service_created, true),
                  Assign(&m_request_find_service_destroyed, false), std::move(rsf),
                  Return(ByMove(std::make_unique<Handle_mock>(m_request_find_service_destroyed)))));
}

void Bridge_data::expect_callbacks(Expect const& expect, Connector_factory const& connector_factory,
                                   Subscribe_find_service_function const& sfs_callback) {
    if (enable_subscribe_find_service(expect)) {
        expect_subscribe_find_service(connector_factory.get_configuration().get_interface(),
                                      connector_factory.get_instance(), sfs_callback);
    }
    if (enable_request_find_service(expect)) {
        expect_request_find_service(connector_factory.get_configuration(),
                                    connector_factory.get_instance());
    }
}

Bridge_data::Subscribe_find_service_function Bridge_data::sfs_do_nothing() {
    return [](auto /*unused*/, auto /*unused*/, auto /*unused*/) {
        return std::make_unique<Find_subscription_handle>();
    };
}

Bridge_data::Bridge_data(Creation_sequence const& sequence, Expect const& expect,
                         Connector_factory& connector_factory,
                         std::function<void(Bridge_data&)>&& ctor_callback,
                         Subscribe_find_service_function const& sfs_callback) {
    score::Result<Service_bridge_registration> tmp_registration =
        score::MakeUnexpected(Construction_error::callback_missing);
    if (Creation_sequence::bridge_then_expect == sequence) {
        tmp_registration = register_at_runtime(connector_factory);
        ctor_callback(*this);
        expect_callbacks(expect, connector_factory, sfs_callback);
    } else {
        ctor_callback(*this);
        expect_callbacks(expect, connector_factory, sfs_callback);
        tmp_registration = register_at_runtime(connector_factory);
        EXPECT_TRUE(!enable_request_find_service(expect) || !m_request_find_service_destroyed);
        EXPECT_TRUE(!enable_subscribe_find_service(expect) || !m_subscribe_find_service_destroyed);
    }
    EXPECT_TRUE(tmp_registration);
    m_bridge_registration = std::move(tmp_registration).value();
    EXPECT_NE(nullptr, m_bridge_registration);
}

Bridge_data::~Bridge_data() {
    wait_for_atomics(m_request_find_service_destroyed, m_subscribe_find_service_destroyed);
}

void Bridge_data::no_destroyed_check() {
    m_request_find_service_destroyed = true;
    m_subscribe_find_service_destroyed = true;
}

void Bridge_data::find_service(Service_interface const& interface, Service_instance const& instance,
                               Find_result_status const& status) const {
    wait_for_atomics(get_subscribe_find_service_created());
    ASSERT_TRUE(get_subscribe_find_service_created());
    m_find_result_callback(interface, instance, status);
}

std::atomic<bool> const& Bridge_data::get_request_find_service_created() const {
    return m_request_find_service_created;
}

std::atomic<bool> const& Bridge_data::get_request_find_service_destroyed() const {
    return m_request_find_service_destroyed;
}

std::atomic<bool> const& Bridge_data::get_subscribe_find_service_created() const {
    return m_subscribe_find_service_created;
}

std::atomic<bool> const& Bridge_data::get_subscribe_find_service_destroyed() const {
    return m_subscribe_find_service_destroyed;
}

std::optional<Bridge_identity> Bridge_data::get_identity() const {
    return std::optional<Bridge_identity>{m_identity};
}

}  // namespace ac
