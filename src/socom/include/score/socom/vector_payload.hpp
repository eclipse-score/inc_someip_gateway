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

#ifndef SCORE_SOCOM_VECTOR_PAYLOAD_HPP
#define SCORE_SOCOM_VECTOR_PAYLOAD_HPP

#include <score/socom/payload.hpp>

#include <cstddef>
#include <vector>

namespace score::socom {

/// \brief Alias for payload data.
using Vector_buffer = std::vector<Payload::Byte>;

/// \brief Creates a vector payload by moving the given data.
/// \param buffer Payload data.
/// \return A pointer to a Payload object.
Payload::Sptr make_vector_payload(Vector_buffer buffer);

/// \brief Creates a vector payload by moving the given data.
/// \param header_size Size of header data.
/// \param buffer Payload data.
/// \return A pointer to a Payload object.
Payload::Sptr make_vector_payload(std::size_t header_size, Vector_buffer buffer);

Payload::Sptr make_vector_payload(std::size_t lead_offset, std::size_t header_size,
                                  Vector_buffer buffer);

/// \brief Creates vector payload from a container.
/// \param container Reference container.
/// \tparam C Container type.
/// \return A pointer to a Payload object.
template<typename C>
inline Payload::Sptr make_vector_payload(C const& container)
{
    return make_vector_payload(Vector_buffer{std::begin(container), std::end(container)});
}

} // namespace score::socom

#endif // SCORE_SOCOM_VECTOR_PAYLOAD_FACTORY_HPP
