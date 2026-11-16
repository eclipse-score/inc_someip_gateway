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

#include "gtest/gtest.h"
#include "score/socom/client_connector.hpp"
#include "score/socom/clients_t.hpp"
#include "score/socom/event.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/server_t.hpp"
#include "score/socom/single_connection_test_fixture.hpp"
#include "score/socom/utilities.hpp"

using ::testing::_;

namespace score::socom {

TEST_F(SingleConnectionTest, SCOffered) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "requirements-analysis");
    Server_data server{connector_factory};

    Client_data client0{connector_factory};
}

TEST_F(SingleConnectionTest, OfferSC) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "requirements-analysis");
    Client_data client0{connector_factory, Client_data::no_connect};

    auto const& available = client0.expect_service_state_change(Service_state::available);

    Server_data server{connector_factory};

    wait_for_atomics(available);

    client0.expect_service_state_change(Service_state::not_available);
}

TEST_F(SingleConnectionTest, StopOfferSC) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "requirements-analysis");
    Server_data server{connector_factory};
    Client_data client0{connector_factory};

    auto const& not_available = client0.expect_service_state_change(Service_state::not_available);

    auto scd = server.disable();

    wait_for_atomics(not_available);
}

TEST_F(SingleConnectionTest, ReOfferSC) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "requirements-analysis");
    Server_data server{connector_factory};
    Client_data client0{connector_factory};

    auto const& not_available = client0.expect_service_state_change(Service_state::not_available);
    auto scd = server.disable();
    wait_for_atomics(not_available);

    auto const& available = client0.expect_service_state_change(Service_state::available);

    server.enable(std::move(scd));
    wait_for_atomics(available);
}

TEST_F(SingleConnectionTest, DestroyingConnectedClientConnectorWorks) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "requirements-analysis");
    Server_data server{connector_factory};
    auto client0 = std::make_unique<Client_data>(connector_factory);

    client0 = nullptr;
}

TEST_F(SingleConnectionTest, ServerIsDestroyedBeforeClient) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "requirements-analysis");
    auto server = std::make_unique<Server_data>(connector_factory);
    Client_data client0{connector_factory};

    client0.expect_service_state_change(Service_state::not_available);

    server = nullptr;
}

TEST_F(SingleConnectionTest, ServerIsDestroyedBeforeClientAndEventSubsciption) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "requirements-analysis");
    auto server = std::make_unique<Server_data>(connector_factory);
    Client_data client0{connector_factory};

    auto const& wait_subscribed =
        server->expect_on_event_subscription_change(event_id, Event_state::subscribed);
    auto const sub0 = client0.create_event_subscription(event_id);
    wait_for_atomics(wait_subscribed);

    client0.expect_service_state_change(Service_state::not_available);

    server = nullptr;
}

TEST_F(SingleConnectionTest, DisablingServerMakesClientLoseEventSubscription) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "error-guessing");
    Server_data server{connector_factory};
    Client_data client0{connector_factory};

    auto const& wait_subscribed =
        server.expect_on_event_subscription_change(event_id, Event_state::subscribed);
    auto const sub0 = client0.create_event_subscription(event_id);
    wait_for_atomics(wait_subscribed);

    // check event transmission
    auto const& event_received = client0.expect_event_update(event_id, real_payload);
    server.update_event(event_id, real_payload);
    wait_for_atomics(event_received);

    client0.expect_service_state_change(Service_state::not_available);
    auto dsc = server.disable();
    client0.expect_service_state_change(Service_state::available);
    server.enable(std::move(dsc));

    // GMock would fail, if these were received
    server.update_event(event_id, real_payload);
}

}  // namespace score::socom
