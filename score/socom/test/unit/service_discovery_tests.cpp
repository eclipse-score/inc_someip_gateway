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
 * AI Disclosure: Assisted by OpenAI Codex (GPT-6.1 Sol).
 ********************************************************************************/

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <thread>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"
#include "score/socom/runtime.hpp"
#include "score/socom/service_interface_identifier.hpp"
#include "score/socom/socom_mocks.hpp"
#include "score/socom/utilities.hpp"

namespace score::socom {
namespace {

TEST(ServiceIdentityTest, MinorVersionsShareCanonicalIdentityAndHash) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies",
                   "comp_req__socom__canonical_identity, comp_req__socom__connector_compatibility");
    Service_interface const old_contract{std::string_view{"service"}, {1U, 0U}};
    Service_interface const new_contract{std::string_view{"service"}, {1U, 65535U}};
    EXPECT_EQ(old_contract.get_identifier(), new_contract.get_identifier());
    std::unordered_set<Service_interface_identifier> services{old_contract.get_identifier(),
                                                              new_contract.get_identifier()};
    EXPECT_EQ(1U, services.size());
    EXPECT_FALSE(old_contract == new_contract);
}

TEST(ServiceIdentityTest, DifferentIdsAndMajorsRemainSeparate) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__canonical_identity");
    Service_interface_identifier const first{std::string_view{"service"}, 1U};
    Service_interface_identifier const other_id{std::string_view{"other"}, 1U};
    Service_interface_identifier const other_major{std::string_view{"service"}, 2U};
    std::set<Service_interface_identifier> const ordered{first, other_id, other_major};
    std::unordered_set<Service_interface_identifier> const hashed{first, other_id, other_major};
    EXPECT_EQ(3U, ordered.size());
    EXPECT_EQ(3U, hashed.size());
}

TEST(ServiceIdentityTest, AllConstructorsUseRegisteredStringIdentity) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__canonical_identity");
    auto const registered = service_id_registry().insert(std::string_view{"service"}).first;
    Service_interface_identifier const by_registry{registered, 1U};
    Service_interface_identifier const by_view{std::string_view{"service"}, 1U};
    Service_interface_identifier const by_string{std::string{"service"}, 1U};
    Service_interface_identifier const by_literal{std::string_view{"service"}, Literal_tag{}, 1U};
    EXPECT_EQ(by_registry, by_view);
    EXPECT_EQ(by_view, by_string);
    EXPECT_EQ(by_string, by_literal);
    EXPECT_EQ(registered.data(), by_literal.id.data());
}

TEST(ServiceIdentityTest, FullInstanceIdentityIncludesActualMinorAndInstance) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__instance_identity");
    Service_interface_identifier const service{std::string_view{"service"}, 1U};
    Service_instance const instance{std::string_view{"first"}};
    Service_instance_identifier const first{service, 0U, instance};
    Service_instance_identifier const different_minor{service, 1U, instance};
    Service_instance_identifier const different_instance{
        service, 0U, Service_instance{std::string_view{"second"}}};
    std::set<Service_instance_identifier> const ordered{first, different_minor, different_instance};
    std::unordered_set<Service_instance_identifier> const hashed{first, first, different_minor,
                                                                 different_instance};
    EXPECT_EQ(3U, ordered.size());
    EXPECT_EQ(3U, hashed.size());
    EXPECT_FALSE(first == different_minor);
    EXPECT_FALSE(first == different_instance);
}

class FindServiceCompatibilityTest
    : public ::testing::TestWithParam<std::tuple<std::uint16_t, std::uint16_t>> {};

TEST_P(FindServiceCompatibilityTest, MinimumMinorUsesCompatibleOfferSemantics) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__discovery_filters");
    auto const requested = std::get<0>(GetParam());
    auto const offered = std::get<1>(GetParam());
    Service_interface_identifier const service{std::string_view{"service"}, 1U};
    Find_service_request const request{service, requested, std::nullopt};
    Service_instance_identifier const offer{service, offered,
                                            Service_instance{std::string_view{"first"}}};
    EXPECT_EQ(requested <= offered, request.matches(offer));
}

INSTANTIATE_TEST_SUITE_P(Boundaries, FindServiceCompatibilityTest,
                         ::testing::Combine(::testing::Values<std::uint16_t>(0U, 1U, 42U, 65535U),
                                            ::testing::Values<std::uint16_t>(0U, 1U, 42U, 65535U)));

TEST(FindServiceRequestTest, MissingFiltersAcceptEveryMinorAndInstance) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__discovery_filters");
    Service_interface_identifier const service{std::string_view{"service"}, 1U};
    Find_service_request const request{service, std::nullopt, std::nullopt};
    EXPECT_TRUE(request.matches({service, 0U, Service_instance{std::string_view{""}}}));
    EXPECT_TRUE(request.matches({service, 65535U, Service_instance{std::string_view{"other"}}}));
}

TEST(FindServiceRequestTest, IdAndMajorMustMatchExactlyIncluding255) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__discovery_filters");
    Find_service_request const request{
        {std::string_view{"service"}, 255U}, std::nullopt, std::nullopt};
    EXPECT_TRUE(request.matches(
        {{std::string_view{"service"}, 255U}, 0U, Service_instance{std::string_view{"first"}}}));
    EXPECT_FALSE(request.matches(
        {{std::string_view{"service"}, 1U}, 0U, Service_instance{std::string_view{"first"}}}));
    EXPECT_FALSE(request.matches(
        {{std::string_view{"other"}, 255U}, 0U, Service_instance{std::string_view{"first"}}}));
}

TEST(FindServiceRequestTest, EmptyInstanceFilterIsAnExactId) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__discovery_filters");
    Service_interface_identifier const service{std::string_view{"service"}, 1U};
    Find_service_request const request{service, std::nullopt,
                                       Service_instance{std::string_view{""}}};
    EXPECT_TRUE(request.matches({service, 0U, Service_instance{std::string_view{""}}}));
    EXPECT_FALSE(request.matches({service, 0U, Service_instance{std::string_view{"other"}}}));
}

class FindServiceRuntimeTest : public ::testing::Test {
   protected:
    Server_connector_callbacks_mock callbacks;
    Runtime::Uptr runtime = create_runtime();
    std::vector<Enabled_server_connector::Uptr> servers;
    std::array<std::optional<Service_instance_identifier>, 4U> results{};
    Service_interface_identifier const identity{std::string_view{"service"}, 1U};

    void add_server(std::uint16_t minor, std::string_view instance, std::uint16_t major = 1U,
                    std::string_view service = "service") {
        Server_service_interface_definition const configuration{
            Service_interface{service, {major, minor}}, to_num_of_methods(1U),
            to_num_of_events(1U)};
        auto connector = runtime->make_server_connector(configuration, Service_instance{instance},
                                                        create_server_callbacks(callbacks));
        ASSERT_TRUE(connector.has_value());
        servers.emplace_back(Disabled_server_connector::enable(std::move(connector).value()));
        ASSERT_NE(nullptr, servers.back());
    }

    std::size_t find(std::optional<std::uint16_t> minor = std::nullopt,
                     std::optional<Service_instance> instance = std::nullopt) {
        return runtime->find_service({identity, minor, instance}, results.data(), results.size());
    }
};

TEST_F(FindServiceRuntimeTest, NoOffersReturnsZeroAndClearsOldSlots) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies",
                   "comp_req__socom__available_offers, comp_req__socom__bounded_discovery");
    results[0] =
        Service_instance_identifier{identity, 7U, Service_instance{std::string_view{"stale"}}};
    EXPECT_EQ(0U, find());
    EXPECT_FALSE(results[0].has_value());
}

TEST_F(FindServiceRuntimeTest, ReportsActualMinorOfEachInstanceAndIgnoresOtherServices) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies",
                   "comp_req__socom__available_offers, comp_req__socom__instance_identity");
    add_server(2U, "first");
    add_server(7U, "second");
    add_server(9U, "first", 2U);
    add_server(9U, "first", 1U, "other");
    ASSERT_EQ(2U, find());
    ASSERT_TRUE(results[0].has_value());
    ASSERT_TRUE(results[1].has_value());
    std::set<Service_instance_identifier> const actual{*results[0], *results[1]};
    std::set<Service_instance_identifier> const expected{
        {identity, 2U, Service_instance{std::string_view{"first"}}},
        {identity, 7U, Service_instance{std::string_view{"second"}}}};
    EXPECT_EQ(expected, actual);
    EXPECT_FALSE(results[2].has_value());
}

TEST_F(FindServiceRuntimeTest, AppliesMinorAndInstanceFiltersTogether) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__discovery_filters");
    add_server(2U, "first");
    add_server(7U, "second");
    ASSERT_EQ(1U, find(3U));
    EXPECT_EQ(7U, results[0]->minor_version);
    EXPECT_EQ(0U, find(3U, Service_instance{std::string_view{"first"}}));
    ASSERT_EQ(1U, find(7U, Service_instance{std::string_view{"second"}}));
    EXPECT_EQ(0U, find(8U));
    ASSERT_EQ(1U, find(std::nullopt, Service_instance{std::string_view{"first"}}));
    EXPECT_EQ(2U, results[0]->minor_version);
}

TEST_F(FindServiceRuntimeTest, CountOnlyAndTruncationReportAllMatchingOffers) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__bounded_discovery");
    add_server(2U, "first");
    add_server(7U, "second");
    Find_service_request const request{identity, std::nullopt, std::nullopt};
    EXPECT_EQ(2U, runtime->find_service(request, nullptr, 0U));
    results[1] =
        Service_instance_identifier{identity, 99U, Service_instance{std::string_view{"untouched"}}};
    EXPECT_EQ(2U, runtime->find_service(request, results.data(), 1U));
    EXPECT_TRUE(results[0].has_value());
    ASSERT_TRUE(results[1].has_value());
    EXPECT_EQ(99U, results[1]->minor_version);
}

TEST_F(FindServiceRuntimeTest, DisabledAndDestroyedServersAreNotDiscoverable) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__available_offers");
    Server_service_interface_definition const configuration{
        Service_interface{std::string_view{"service"}, {1U, 2U}}, to_num_of_methods(1U),
        to_num_of_events(1U)};
    auto connector =
        runtime->make_server_connector(configuration, Service_instance{std::string_view{"first"}},
                                       create_server_callbacks(callbacks));
    ASSERT_TRUE(connector.has_value());
    EXPECT_EQ(0U, find());
    auto enabled = Disabled_server_connector::enable(std::move(connector).value());
    EXPECT_EQ(1U, find());
    enabled.reset();
    EXPECT_EQ(0U, find());
    EXPECT_FALSE(results[0].has_value());
}

TEST_F(FindServiceRuntimeTest, ReplacingAnOfferUsesTheNewMinorInsteadOfTheIndexKey) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies",
                   "comp_req__socom__available_offers, comp_req__socom__registration_identity");
    add_server(2U, "first");
    ASSERT_EQ(1U, find());
    EXPECT_EQ(2U, results[0]->minor_version);
    servers.clear();
    add_server(7U, "first");
    ASSERT_EQ(1U, find(7U));
    EXPECT_EQ(7U, results[0]->minor_version);
    EXPECT_EQ(0U, find(8U));
}

TEST_F(FindServiceRuntimeTest, MinimumAndMaximumMinorHaveNoSentinelMeaning) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__discovery_filters");
    add_server(0U, "first");
    add_server(std::numeric_limits<std::uint16_t>::max(), "second");
    EXPECT_EQ(2U, find(0U));
    ASSERT_EQ(1U, find(std::numeric_limits<std::uint16_t>::max()));
    EXPECT_EQ(std::numeric_limits<std::uint16_t>::max(), results[0]->minor_version);
}

TEST_F(FindServiceRuntimeTest, ClientOnlyRecordDoesNotAdvertiseAnOfferOrItsMinor) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__available_offers");
    Client_connector_callbacks_mock client_callbacks;
    Service_interface_definition const configuration{
        Service_interface{std::string_view{"service"}, {1U, 0U}}};
    auto client =
        runtime->make_client_connector(configuration, Service_instance{std::string_view{"first"}},
                                       create_client_callbacks(client_callbacks));
    ASSERT_TRUE(client.has_value());
    EXPECT_EQ(0U, find());
    client.value().reset();
    add_server(7U, "first");
    ASSERT_EQ(1U, find(7U));
    EXPECT_EQ(7U, results[0]->minor_version);
}

TEST_F(FindServiceRuntimeTest, SnapshotCanRunWhileOffersAreRegisteredAndDestroyed) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__discovery_synchronization");
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> observations{0U};
    Find_service_request const request{identity, std::nullopt, std::nullopt};
    std::thread observer{[&]() {
        std::array<std::optional<Service_instance_identifier>, 1U> snapshot{};
        while (!stop.load()) {
            auto const count = runtime->find_service(request, snapshot.data(), snapshot.size());
            EXPECT_LE(count, 1U);
            EXPECT_EQ(count == 1U, snapshot[0].has_value());
            if (count == 1U && snapshot[0]) {
                EXPECT_EQ(identity, snapshot[0]->interface);
                EXPECT_LE(snapshot[0]->minor_version, 99U);
            } else {
                EXPECT_FALSE(snapshot[0].has_value());
            }
            ++observations;
        }
    }};
    while (observations.load() == 0U) {
        std::this_thread::yield();
    }
    for (std::uint16_t minor = 0U; minor < 100U; ++minor) {
        add_server(minor, "first");
        servers.clear();
    }
    stop.store(true);
    observer.join();
    EXPECT_GT(observations.load(), 0U);
    EXPECT_EQ(0U, find());
}

using FindServiceRuntimeDeathTest = FindServiceRuntimeTest;

TEST_F(FindServiceRuntimeDeathTest, InvalidOutputStorageIsRejected) {
    RecordProperty("TestType", "requirements-based");
    RecordProperty("DerivationTechnique", "boundary-values,equivalence-classes,design-analysis");
    RecordProperty("PartiallyVerifies", "comp_req__socom__bounded_discovery");
    Find_service_request const request{identity, std::nullopt, std::nullopt};
    EXPECT_DEATH((void)runtime->find_service(request, nullptr, 1U), "");
}

}  // namespace
}  // namespace score::socom
