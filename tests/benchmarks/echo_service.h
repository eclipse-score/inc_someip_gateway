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
#ifndef TESTS_BENCHMARKS_ECHO_SERVICE
#define TESTS_BENCHMARKS_ECHO_SERVICE

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>

#include "score/mw/com/types.h"
#include "src/serializer/pre_serialized_data.h"

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
template <PayloadSize PayloadBytes>
struct EchoMessage {
    std::uint64_t sequence_id;
    std::uint64_t timestamp_ns;
    PayloadSize payload_size;
    std::uint32_t actual_size;
    std::uint8_t payload[static_cast<std::size_t>(PayloadBytes)];
};

// Type aliases for specific payload sizes
using EchoMessageTiny = EchoMessage<PayloadSize::Tiny>;
using EchoMessageSmall = EchoMessage<PayloadSize::Small>;
using EchoMessageMedium = EchoMessage<PayloadSize::Medium>;
using EchoMessageLarge = EchoMessage<PayloadSize::Large>;
using EchoMessageXLarge = EchoMessage<PayloadSize::XLarge>;
using EchoMessageXXLarge = EchoMessage<PayloadSize::XXLarge>;

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

// Pre-serialized data interfaces for all payload sizes
template <PayloadSize PayloadBytes>
using EchoMessagePreSerialized =
    score::someip_gateway::serializer::PreSerializedData<sizeof(EchoMessage<PayloadBytes>)>;

using EchoMessagePreSerializedTiny = EchoMessagePreSerialized<PayloadSize::Tiny>;
using EchoMessagePreSerializedSmall = EchoMessagePreSerialized<PayloadSize::Small>;
using EchoMessagePreSerializedMedium = EchoMessagePreSerialized<PayloadSize::Medium>;
using EchoMessagePreSerializedLarge = EchoMessagePreSerialized<PayloadSize::Large>;
using EchoMessagePreSerializedXLarge = EchoMessagePreSerialized<PayloadSize::XLarge>;
using EchoMessagePreSerializedXXLarge = EchoMessagePreSerialized<PayloadSize::XXLarge>;

template <typename Trait>
class EchoRequestPreSerializedInterface : public Trait::Base {
   public:
    using Trait::Base::Base;

    typename Trait::template Event<EchoMessagePreSerializedTiny> echo_request_tiny_{
        *this, "echo_request_tiny"};
    typename Trait::template Event<EchoMessagePreSerializedSmall> echo_request_small_{
        *this, "echo_request_small"};
    typename Trait::template Event<EchoMessagePreSerializedMedium> echo_request_medium_{
        *this, "echo_request_medium"};
    typename Trait::template Event<EchoMessagePreSerializedLarge> echo_request_large_{
        *this, "echo_request_large"};
    typename Trait::template Event<EchoMessagePreSerializedXLarge> echo_request_xlarge_{
        *this, "echo_request_xlarge"};
    typename Trait::template Event<EchoMessagePreSerializedXXLarge> echo_request_xxlarge_{
        *this, "echo_request_xxlarge"};
};
template <typename Trait>
class EchoResponsePreSerializedInterface : public Trait::Base {
   public:
    using Trait::Base::Base;

    typename Trait::template Event<
        score::someip_gateway::serializer::PreSerializedData<sizeof(EchoResponseTiny)>>
        echo_response_tiny_{*this, "echo_response_tiny"};
    typename Trait::template Event<
        score::someip_gateway::serializer::PreSerializedData<sizeof(EchoResponseSmall)>>
        echo_response_small_{*this, "echo_response_small"};
    typename Trait::template Event<
        score::someip_gateway::serializer::PreSerializedData<sizeof(EchoResponseMedium)>>
        echo_response_medium_{*this, "echo_response_medium"};
    typename Trait::template Event<
        score::someip_gateway::serializer::PreSerializedData<sizeof(EchoResponseLarge)>>
        echo_response_large_{*this, "echo_response_large"};
    typename Trait::template Event<
        score::someip_gateway::serializer::PreSerializedData<sizeof(EchoResponseXLarge)>>
        echo_response_xlarge_{*this, "echo_response_xlarge"};
    typename Trait::template Event<
        score::someip_gateway::serializer::PreSerializedData<sizeof(EchoResponseXXLarge)>>
        echo_response_xxlarge_{*this, "echo_response_xxlarge"};
};
using EchoRequestPreSerializedProxy = score::mw::com::AsProxy<EchoRequestPreSerializedInterface>;
using EchoRequestPreSerializedSkeleton =
    score::mw::com::AsSkeleton<EchoRequestPreSerializedInterface>;
using EchoResponsePreSerializedProxy = score::mw::com::AsProxy<EchoResponsePreSerializedInterface>;
using EchoResponsePreSerializedSkeleton =
    score::mw::com::AsSkeleton<EchoResponsePreSerializedInterface>;

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

constexpr inline std::uint32_t GetSizeFromEnum(PayloadSize size) {
    return static_cast<std::uint32_t>(size);
}

constexpr inline PayloadSize GetEnumFromSize(std::uint32_t size) {
    if (size <= 8) return PayloadSize::Tiny;
    if (size <= 64) return PayloadSize::Small;
    if (size <= 1024) return PayloadSize::Medium;
    if (size <= 8192) return PayloadSize::Large;
    if (size <= 65536) return PayloadSize::XLarge;
    return PayloadSize::XXLarge;
}

template <PayloadSize PayloadBytes>
inline std::uint64_t GetSequenceId(const EchoMessagePreSerialized<PayloadBytes>& message) {
    // TODO: Apply proper deserialization instead of just reinterpreting the bytes. This is just a
    // quick workaround to get the sequence ID for logging purposes without having to implement a
    // full deserialization.
    static_assert(
        sizeof(std::uint64_t) <= EchoMessagePreSerialized<PayloadBytes>::kMaxMessageSize,
        "EchoMessagePreSerialized must be large enough to hold sequence_id for GetSequenceId");
    return *reinterpret_cast<const std::uint64_t*>(&message.data[0]);
}
template <PayloadSize PayloadBytes>
inline void CopyMessageForEcho(EchoMessagePreSerialized<PayloadBytes>& response,
                               const EchoMessagePreSerialized<PayloadBytes>& request) {
    assert(request.size <= response.kMaxMessageSize &&
           "Request size exceeds maximum message size for pre-serialized data");
    response.size = request.size;
    std::memcpy(response.data, request.data, request.size);
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

#endif  // TESTS_BENCHMARKS_ECHO_SERVICE
