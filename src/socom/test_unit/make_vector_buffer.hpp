/********************************************************************************
 * Copyright (c) 2025 Elektrobit Automotive GmbH
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

#ifndef SCORE_SOCOM_TEST_MAKE_VECTOR_BUFFER_HPP
#define SCORE_SOCOM_TEST_MAKE_VECTOR_BUFFER_HPP

#include <score/socom/vector_payload.hpp>

#include <type_traits>

namespace score::socom::test {

// Creates a Vector_buffer from a list of unsigned integral type elements for testing purposes.
template<typename... Ts>
Vector_buffer make_vector_buffer(Ts... args) noexcept
{
    static_assert((std::is_unsigned_v<Ts> && ...),
                  "All arguments must be unsigned integral types.");
    return {static_cast<Payload::Byte>(args)...};
}

} // namespace score::socom::test

#endif // SCORE_SOCOM_TEST_MAKE_VECTOR_BUFFER_HPP
