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

#include "single_connection_test_fixture.hpp"

using ::score::socom::Event_id;
using ::score::socom::make_vector_buffer;
using ::score::socom::make_vector_payload;
using ::score::socom::Method_id;
using ::score::socom::Payload;

namespace ac {

Payload::Sptr const& input_data() {
    static Payload::Sptr const data = make_vector_payload(make_vector_buffer(9U, 0U, 0U, 1U));
    return data;
}

Payload::Sptr const& error_data() {
    static Payload::Sptr const data = make_vector_payload(make_vector_buffer(1U, 0U, 0U, 6U));
    return data;
}

Method_id const SingleConnectionTest::method_id;
Event_id const SingleConnectionTest::event_id;

}  // namespace ac
