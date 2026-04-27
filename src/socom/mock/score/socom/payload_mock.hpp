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

#ifndef SCORE_SOCOM_PAYLOAD_MOCK_HPP
#define SCORE_SOCOM_PAYLOAD_MOCK_HPP

#include <memory>
#include <score/socom/payload.hpp>
#include <vector>

namespace score::socom {

/// \brief Creates a test Payload backed by a heap-allocated buffer.
/// \param size Size of the payload buffer in bytes.
/// \return A Payload object.
inline Payload make_test_payload(std::size_t size = 0) {
    auto buf = std::make_unique<std::vector<std::byte>>(size);
    auto span = Payload::Writable_span{buf->data(), static_cast<Payload::Writable_span::size_type>(buf->size())};
    return Payload{span, kNoSlotHandle, [buf = std::move(buf)]() {}};
}

/// \brief Creates a test Writable_payload backed by a heap-allocated buffer.
/// \param size Size of the payload buffer in bytes.
/// \return A Writable_payload object.
inline Writable_payload make_test_writable_payload(std::size_t size = 0) {
    auto buf = std::make_unique<std::vector<std::byte>>(size);
    auto span = Writable_payload::Writable_span{buf->data(), static_cast<Writable_payload::Writable_span::size_type>(buf->size())};
    return Writable_payload{span, kNoSlotHandle, [buf = std::move(buf)]() {}};
}

}  // namespace score::socom

#endif  // SCORE_SOCOM_PAYLOAD_MOCK_HPP
