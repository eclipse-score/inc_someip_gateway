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
#ifndef TESTS_INTEGRATION_TEST_EVENT_EXCHANGE_SERVICE_INTERFACE_H
#define TESTS_INTEGRATION_TEST_EVENT_EXCHANGE_SERVICE_INTERFACE_H

// The PingBody layout is mirrored by event_exchange/someip_peer.py.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

#include "score/serializer/pre_serialized_data.h"

namespace score::event_exchange {

// mw::com instance specifiers — must match the serviceInstances entries in the
// per-process mw_com_config.json files.
constexpr std::string_view kTxInstanceSpecifier{"/event_exchange/tx"};
constexpr std::string_view kRxInstanceSpecifier{"/event_exchange/rx"};

// Event name — must match the eventName in both mw_com_config.json and the
// event_name in mw_someip_config.json.
constexpr std::string_view kEventName{"ping"};

// Maximum payload bytes on the SOME/IP wire. Must equal the max_message_size
// field in the NullSerializerConfig for both the TX and RX service events.
constexpr std::size_t kMaxMessageSize{256};

// mw::com event type for both TX and RX. The serializer consumes this layout
// directly: it reads `size` and copies `size` bytes from `data` onto the wire
// on the send side, and inversely on the receive side.
using PingPayload = score::someip_gateway::serializer::PreSerializedData<kMaxMessageSize>;

// Fixed marker. Present in every valid payload; the receiver uses it as the
// first filter before the byte-for-byte comparison.
constexpr std::uint64_t kPayloadMarker{0x4558'4348'1700'B1D6ULL};

// Deterministic 64-byte user payload. The counter distinguishes consecutive
// samples; the padding is a pure function of the counter so sender and
// receiver can reproduce it independently and memcmp it.
struct PingBody {
    std::uint64_t marker;
    std::uint64_t counter;
    std::array<std::byte, 48> padding;
};
static_assert(sizeof(PingBody) == 64, "PingBody byte layout must be stable across the wire");

constexpr std::size_t kPingBodySize{sizeof(PingBody)};

constexpr PingBody make_body(std::uint64_t counter) noexcept {
    PingBody body{};
    body.marker = kPayloadMarker;
    body.counter = counter;
    for (std::size_t i = 0; i < body.padding.size(); ++i) {
        body.padding[i] = static_cast<std::byte>((counter + i) & 0xFFU);
    }
    return body;
}

// Write a PingBody into a PingPayload sample so the serializer transmits
// exactly kPingBodySize bytes on the wire.
inline void fill_sample(PingPayload& sample, std::uint64_t counter) noexcept {
    const auto body = make_body(counter);
    sample.size = kPingBodySize;
    std::memcpy(sample.data, &body, kPingBodySize);
}

// Byte-for-byte oracle against the next sample in the known transmitted
// sequence. Deriving expected bytes from the observed counter would allow a
// coherent rewrite to another valid body, so the expected counter comes from
// receiver state instead.
//
// Returns expected_counter on match, std::nullopt otherwise.
inline std::optional<std::uint64_t> matched_expected_counter(
    const PingPayload& sample, std::uint64_t expected_counter) noexcept {
    if (sample.size != kPingBodySize) {
        return std::nullopt;
    }
    PingBody observed{};
    std::memcpy(&observed, sample.data, kPingBodySize);
    if (observed.marker != kPayloadMarker) {
        return std::nullopt;
    }
    const auto expected = make_body(expected_counter);
    if (std::memcmp(&observed, &expected, kPingBodySize) != 0) {
        return std::nullopt;
    }
    return expected_counter;
}

}  // namespace score::event_exchange

#endif  // TESTS_INTEGRATION_TEST_EVENT_EXCHANGE_SERVICE_INTERFACE_H
