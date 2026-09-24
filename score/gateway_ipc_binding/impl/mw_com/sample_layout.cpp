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

#include "sample_layout.hpp"

#include <cstring>
#include <score/assert.hpp>

namespace score::gateway_ipc_binding::mw_com {

std::size_t addressed_sample_size(Event_config const& event) noexcept {
    return kSample_prefix_size + event.header_size + event.max_payload_size;
}

score::mw::com::DataTypeMetaInfo sample_meta_info(Event_config const& event) noexcept {
    return score::mw::com::DataTypeMetaInfo{sample_size(event), kSample_alignment};
}

void write_payload_length(std::byte* const sample_base, std::size_t const length) noexcept {
    SCORE_LANGUAGE_FUTURECPP_ASSERT(sample_base != nullptr);
    SCORE_LANGUAGE_FUTURECPP_ASSERT(length <= kMax_payload_length);

    auto const encoded = static_cast<std::uint32_t>(length);
    // The prefix is only ever read back by the peer binding on the same host, so the native byte
    // order is the wire format.
    std::memcpy(sample_base, &encoded, sizeof(encoded));

    // The second half of the prefix is reserved. Zero it so that it stays deterministic, LoLa
    // slots are recycled and would otherwise carry the previous sample's bytes.
    std::uint32_t const reserved{0U};
    std::memcpy(sample_base + sizeof(encoded), &reserved, sizeof(reserved));
}

std::size_t read_payload_length(std::byte const* const sample_base) noexcept {
    SCORE_LANGUAGE_FUTURECPP_ASSERT(sample_base != nullptr);

    std::uint32_t encoded{0U};
    std::memcpy(&encoded, sample_base, sizeof(encoded));
    return static_cast<std::size_t>(encoded);
}

}  // namespace score::gateway_ipc_binding::mw_com
