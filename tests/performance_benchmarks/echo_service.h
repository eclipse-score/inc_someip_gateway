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
#ifndef TESTS_PERFORMANCE_BENCHMARKS_ECHO_SERVICE
#define TESTS_PERFORMANCE_BENCHMARKS_ECHO_SERVICE

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>

#include "score/mw/com/types.h"

namespace echo_service {

enum class PayloadSize : std::uint32_t {
    Tiny = 8,
    Small = 64,
    Medium = 1024,
    Large = 8192,
    XLarge = 65536,
    XXLarge = 1048576
};

// Template-based message structure for all payload sizes
template <std::size_t PayloadBytes>
struct EchoMessage {
    std::uint64_t sequence_id;
    std::uint64_t timestamp_ns;
    PayloadSize payload_size;
    std::uint32_t actual_size;
    std::uint8_t payload[PayloadBytes];
};

// Type aliases for specific payload sizes
using EchoMessageTiny = EchoMessage<8>;
using EchoMessageSmall = EchoMessage<64>;
using EchoMessageMedium = EchoMessage<1024>;
using EchoMessageLarge = EchoMessage<8192>;
using EchoMessageXLarge = EchoMessage<65536>;
using EchoMessageXXLarge = EchoMessage<1048576>;

// Type aliases for request/response pairs
using EchoRequestTiny = EchoMessageTiny;
using EchoResponseTiny = EchoMessageTiny;
using EchoRequestSmall = EchoMessageSmall;
using EchoResponseSmall = EchoMessageSmall;
using EchoRequestMedium = EchoMessageMedium;
using EchoResponseMedium = EchoMessageMedium;
using EchoRequestLarge = EchoMessageLarge;
using EchoResponseLarge = EchoMessageLarge;
using EchoRequestXLarge = EchoMessageXLarge;
using EchoResponseXLarge = EchoMessageXLarge;
using EchoRequestXXLarge = EchoMessageXXLarge;
using EchoResponseXXLarge = EchoMessageXXLarge;

template <typename Trait>
class EchoRequestInterface : public Trait::Base {
   public:
    using Trait::Base::Base;

    typename Trait::template Event<EchoRequestTiny> echo_request_tiny_{*this, "echo_request_tiny"};
    typename Trait::template Event<EchoRequestSmall> echo_request_small_{*this,
                                                                         "echo_request_small"};
    typename Trait::template Event<EchoRequestMedium> echo_request_medium_{*this,
                                                                           "echo_request_medium"};
    typename Trait::template Event<EchoRequestLarge> echo_request_large_{*this,
                                                                         "echo_request_large"};
    typename Trait::template Event<EchoRequestXLarge> echo_request_xlarge_{*this,
                                                                           "echo_request_xlarge"};
    typename Trait::template Event<EchoRequestXXLarge> echo_request_xxlarge_{
        *this, "echo_request_xxlarge"};
};

template <typename Trait>
class EchoResponseInterface : public Trait::Base {
   public:
    using Trait::Base::Base;

    typename Trait::template Event<EchoResponseTiny> echo_response_tiny_{*this,
                                                                         "echo_response_tiny"};
    typename Trait::template Event<EchoResponseSmall> echo_response_small_{*this,
                                                                           "echo_response_small"};
    typename Trait::template Event<EchoResponseMedium> echo_response_medium_{
        *this, "echo_response_medium"};
    typename Trait::template Event<EchoResponseLarge> echo_response_large_{*this,
                                                                           "echo_response_large"};
    typename Trait::template Event<EchoResponseXLarge> echo_response_xlarge_{
        *this, "echo_response_xlarge"};
    typename Trait::template Event<EchoResponseXXLarge> echo_response_xxlarge_{
        *this, "echo_response_xxlarge"};
};

// Main proxy and skeleton types
using EchoRequestProxy = score::mw::com::AsProxy<EchoRequestInterface>;
using EchoRequestSkeleton = score::mw::com::AsSkeleton<EchoRequestInterface>;
using EchoResponseProxy = score::mw::com::AsProxy<EchoResponseInterface>;
using EchoResponseSkeleton = score::mw::com::AsSkeleton<EchoResponseInterface>;

namespace utils {

inline std::uint64_t GetCurrentTimeNanos() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

inline void FillTestPayload(std::uint8_t* payload, std::uint32_t size,
                            std::uint64_t pattern = 0xDEADBEEF) {
    std::uint8_t base_pattern = static_cast<std::uint8_t>(pattern & 0xFF);
    for (std::uint32_t i{0}; i < size; ++i) {
        payload[i] = static_cast<std::uint8_t>(base_pattern + (i & 0xFF));
    }
}

inline bool VerifyTestPayload(const std::uint8_t* payload, std::uint32_t size,
                              std::uint64_t pattern = 0xDEADBEEF) {
    std::uint8_t base_pattern = static_cast<std::uint8_t>(pattern & 0xFF);
    for (std::uint32_t i{0}; i < size; ++i) {
        if (payload[i] != static_cast<std::uint8_t>(base_pattern + (i & 0xFF))) {
            return false;
        }
    }
    return true;
}

inline std::uint32_t GetSizeFromEnum(PayloadSize size) { return static_cast<std::uint32_t>(size); }

inline PayloadSize GetEnumFromSize(std::uint32_t size) {
    if (size <= 8) return PayloadSize::Tiny;
    if (size <= 64) return PayloadSize::Small;
    if (size <= 1024) return PayloadSize::Medium;
    if (size <= 8192) return PayloadSize::Large;
    if (size <= 65536) return PayloadSize::XLarge;
    return PayloadSize::XXLarge;
}

template <typename ResponseType, typename RequestType>
inline void CopyMessageForEcho(ResponseType& response, const RequestType& request) {
    response.sequence_id = request.sequence_id;
    response.timestamp_ns = request.timestamp_ns;
    response.payload_size = request.payload_size;
    response.actual_size = request.actual_size;
    std::memcpy(response.payload, request.payload, request.actual_size);
}

template <typename MessageType>
inline void FillTestPayload(MessageType& message, std::uint64_t pattern = 0xDEADBEEF) {
    constexpr auto size = sizeof(message.payload);
    message.payload_size = utils::GetEnumFromSize(size);
    message.actual_size = static_cast<std::uint32_t>(size);
    FillTestPayload(message.payload, static_cast<std::uint32_t>(size), pattern);
}

}  // namespace utils

}  // namespace echo_service

#endif  // TESTS_PERFORMANCE_BENCHMARKS_ECHO_SERVICE
