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

#include "clients_t.hpp"
#include "gtest/gtest.h"
#include "server_t.hpp"
#include "single_connection_test_fixture.hpp"
#include "utilities.hpp"

using ::ac::Client_connector_callbacks_mock;
using ::ac::Client_data;
using ::ac::error_data;
using ::ac::input_data;
using ::ac::Server_data;
using ::ac::SingleConnectionTest;
using ::ac::wait_for_atomics;
using ::score::socom::Application_error;
using ::score::socom::Application_return;
using ::score::socom::empty_payload;
using ::score::socom::Error;
using ::score::socom::make_vector_buffer;
using ::score::socom::make_vector_payload;
using ::score::socom::Method_result;
using ::score::socom::Posix_credentials;
using ::score::socom::Vector_buffer;
using ::score::socom::Writable_payload;
using ::testing::_;
using ::testing::Values;
using ::testing::WithParamInterface;

using namespace std::chrono_literals;

namespace {

using Me = Error;
using Ar = score::socom::Application_return;
using Ae = score::socom::Application_error;
using Input_expected = std::tuple<Method_result, Method_result>;

template <typename T, typename U>
Input_expected create_results(T const& result, U const& expected_result) {
    return {Method_result{result}, Method_result{expected_result}};
}

TEST(Method_type_test, method_result_equality_and_inequality_operators) {
    // Create Method_result objects
    auto const ar = Method_result{Application_return{}};
    auto const ae = Method_result{Application_error{}};
    auto const me = Method_result{Error::runtime_error_request_rejected};
    auto const ae_code = Method_result{Application_error{10}};
    auto const ae_payload =
        Method_result{Application_error{make_vector_payload(Vector_buffer{10})}};

    // Test for (in)equality
    EXPECT_EQ(ar, ar);
    EXPECT_EQ(ae, ae);
    EXPECT_FALSE(ae != ae);
    EXPECT_EQ(me, me);
    EXPECT_NE(ar, ae);
    EXPECT_NE(ar, me);
    EXPECT_NE(ae, me);
    EXPECT_FALSE(ae == ae_code);
    EXPECT_TRUE(ae != ae_code);
    EXPECT_FALSE(ae == ae_payload);
    EXPECT_TRUE(ae != ae_payload);

    // Set up test payloads for Application_return tests. header = 2 bytes.
    auto const vector_payload1 = make_vector_payload(2, make_vector_buffer(1U, 2U, 3U, 4U, 5U));
    auto const vector_payload1_copy =
        make_vector_payload(2, make_vector_buffer(1U, 2U, 3U, 4U, 5U));
    auto const vector_payload2 = make_vector_payload(2, make_vector_buffer(9U, 8U, 7U, 6U, 5U));
    auto const vector_payload3 = make_vector_payload(2, make_vector_buffer(1U, 2U, 7U, 6U, 5U));
    auto const vector_payload4 = make_vector_payload(2, make_vector_buffer(9U, 8U, 3U, 4U, 5U));
    auto const ar1 = Application_return{vector_payload1};
    auto const ar1_copy = Application_return{vector_payload1_copy};
    auto const ar2 = Application_return{vector_payload1};
    auto const ar3 = Application_return{vector_payload3};
    auto const ar4 = Application_return{vector_payload4};

    // Test for (in)equality
    EXPECT_TRUE(ar1 == ar2);
    EXPECT_FALSE(ar1 == ar3);
    EXPECT_FALSE(ar1 == ar4);
    EXPECT_FALSE(ar1 != ar1_copy);
}

class MethodCallTest : public SingleConnectionTest, public WithParamInterface<Input_expected> {
   protected:
    Server_data server{connector_factory};
    Client_data client{connector_factory};

    Method_result const& input_result{std::get<0>(GetParam())};
    Method_result const& expected_result{std::get<1>(GetParam())};
};

TEST_P(MethodCallTest, ClientSendsFireAndForgetMethodCallToServer) {
    auto const& call_received =
        server.expect_and_respond_method_calls(1, method_id, input_data(), input_result);

    client.call_method_fire_and_forget(method_id, input_data());
    wait_for_atomics(call_received);
}

TEST_P(MethodCallTest, ClientCallsMethodAndReceivesResponseFromServer) {
    server.expect_and_respond_method_calls(1, method_id, input_data(), input_result);
    client.expect_and_call_method(method_id, input_data(), expected_result);
}

INSTANTIATE_TEST_SUITE_P(
    MethodCallTestInstance, MethodCallTest,
    Values(create_results(Ar{}, Ar{}), create_results(Ar{error_data()}, Ar{error_data()}),
           create_results(Ar{input_data()}, Ar{input_data()}),
           create_results(Ar{empty_payload()}, Ar{empty_payload()}),
           create_results(Me::runtime_error_request_rejected, Me::runtime_error_request_rejected)));

using MethodCallCredentialsTest = SingleConnectionTest;

TEST_F(MethodCallCredentialsTest, ClientCallsMethodServerReceivesCustomClientCredentials) {
    Posix_credentials default_credentials{::getuid(), ::getgid()};
    Posix_credentials client_credentials{::getuid() + 1, ::getgid() + 1};

    Server_data server{connector_factory, connector_factory.get_configuration(),
                       connector_factory.get_instance(), default_credentials};
    Client_data client{connector_factory, connector_factory.get_configuration(),
                       connector_factory.get_instance(), client_credentials};

    auto& credentials_callbacks_mock = server.get_credentials_callbacks();

    EXPECT_CALL(credentials_callbacks_mock,
                on_method_call(_, method_id, input_data(), _, client_credentials, _))
        .Times(1);

    client.call_method(method_id, input_data());
}

TEST_F(MethodCallCredentialsTest, ClientCallsMethodServerReceivesDefaultClientCredentials) {
    Posix_credentials default_credentials{::getuid(), ::getgid()};

    Server_data server{connector_factory, connector_factory.get_configuration(),
                       connector_factory.get_instance(), default_credentials};
    Client_data client{connector_factory, connector_factory.get_configuration(),
                       connector_factory.get_instance(), default_credentials};

    auto& credentials_callbacks_mock = server.get_credentials_callbacks();

    EXPECT_CALL(credentials_callbacks_mock,
                on_method_call(_, method_id, input_data(), _, default_credentials, _))
        .Times(1);

    client.call_method(method_id, input_data());
}

using AllocateMethodPayloadTest = SingleConnectionTest;

TEST_F(AllocateMethodPayloadTest, AllocateMethodPayloadWithOutOfBoundsMethodIdReturnsLogicError) {
    Server_data server{connector_factory};
    Client_data client{connector_factory};

    auto payload = client.allocate_method_payload(method_id + 1);
    EXPECT_FALSE(payload);
    EXPECT_EQ(payload.error(), Error::logic_error_id_out_of_range);
}

TEST_F(AllocateMethodPayloadTest,
       AllocateMethodPayloadWithoutConnectedServerReturnsServiceNotAvailableError) {
    Client_data client{connector_factory, Client_data::no_connect};

    auto payload = client.allocate_method_payload(method_id);
    EXPECT_FALSE(payload);
    EXPECT_EQ(payload.error(), Error::runtime_error_service_not_available);
}

TEST_F(AllocateMethodPayloadTest, AllocateMethodPayloadWithConnectedServerReturnsPayload) {
    Server_data server{connector_factory};
    Client_data client{connector_factory};

    score::Result<std::unique_ptr<Writable_payload>> wpayload = nullptr;

    auto const& expect_method_payload_allocation =
        server.expect_method_allocate_payload(method_id, std::move(wpayload));

    auto payload = client.allocate_method_payload(method_id);
    EXPECT_TRUE(payload);
    EXPECT_EQ(nullptr, payload.value());
    wait_for_atomics(expect_method_payload_allocation);
}

}  // namespace
