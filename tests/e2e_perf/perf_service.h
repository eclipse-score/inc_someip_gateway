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
#ifndef TESTS_E2E_PERF_PERF_SERVICE_H
#define TESTS_E2E_PERF_PERF_SERVICE_H

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "score/mw/com/types.h"
#include "score/serializer/pre_serialized_data.h"

/// Service definitions shared by the end-to-end performance sender and receiver.
///
/// The gateway is configured with the NullSerializer, so the mw::com event type has to be
/// PreSerializedData<N> with N == sizeof(PerfMessage<...>); the application writes the message
/// bytes itself and gatewayd forwards them verbatim.
namespace perf_service {

/// Payload sizes are capped at 1 KiB because SOME/IP-TP is not used and a message has to fit
/// into a single UDP datagram.
enum class PerfPayloadSize : std::uint32_t { Tiny = 8, Small = 64, Medium = 1024 };

template <PerfPayloadSize PayloadBytes>
struct PerfMessage {
    std::uint64_t sequence_id;
    std::uint64_t send_timestamp_ns;
    // Distinguishes runs, so a receiver can drop samples still buffered from an earlier run.
    std::uint32_t run_id;
    std::uint32_t payload_bytes;
    std::uint8_t payload[static_cast<std::size_t>(PayloadBytes)];
};

using PerfMessageTiny = PerfMessage<PerfPayloadSize::Tiny>;
using PerfMessageSmall = PerfMessage<PerfPayloadSize::Small>;
using PerfMessageMedium = PerfMessage<PerfPayloadSize::Medium>;

template <PerfPayloadSize PayloadBytes>
using PerfSample =
    score::someip_gateway::serializer::PreSerializedData<sizeof(PerfMessage<PayloadBytes>)>;

using PerfSampleTiny = PerfSample<PerfPayloadSize::Tiny>;
using PerfSampleSmall = PerfSample<PerfPayloadSize::Small>;
using PerfSampleMedium = PerfSample<PerfPayloadSize::Medium>;

template <typename Trait>
class PerfRequestInterface : public Trait::Base {
   public:
    using Trait::Base::Base;

    typename Trait::template Event<PerfSampleTiny> perf_request_tiny_{*this, "perf_request_tiny"};
    typename Trait::template Event<PerfSampleSmall> perf_request_small_{*this,
                                                                        "perf_request_small"};
    typename Trait::template Event<PerfSampleMedium> perf_request_medium_{*this,
                                                                          "perf_request_medium"};
};

template <typename Trait>
class PerfResponseInterface : public Trait::Base {
   public:
    using Trait::Base::Base;

    typename Trait::template Event<PerfSampleTiny> perf_response_tiny_{*this, "perf_response_tiny"};
    typename Trait::template Event<PerfSampleSmall> perf_response_small_{*this,
                                                                         "perf_response_small"};
    typename Trait::template Event<PerfSampleMedium> perf_response_medium_{*this,
                                                                           "perf_response_medium"};
};

using PerfRequestProxy = score::mw::com::AsProxy<PerfRequestInterface>;
using PerfRequestSkeleton = score::mw::com::AsSkeleton<PerfRequestInterface>;
using PerfResponseProxy = score::mw::com::AsProxy<PerfResponseInterface>;
using PerfResponseSkeleton = score::mw::com::AsSkeleton<PerfResponseInterface>;

/// Selects the event member matching a payload size, so sender and receiver can be written once
/// as a template instead of once per size.
template <PerfPayloadSize PayloadBytes>
struct EventSelector;

template <>
struct EventSelector<PerfPayloadSize::Tiny> {
    template <typename Interface>
    static auto& Request(Interface& interface) {
        return interface.perf_request_tiny_;
    }
    template <typename Interface>
    static auto& Response(Interface& interface) {
        return interface.perf_response_tiny_;
    }
};

template <>
struct EventSelector<PerfPayloadSize::Small> {
    template <typename Interface>
    static auto& Request(Interface& interface) {
        return interface.perf_request_small_;
    }
    template <typename Interface>
    static auto& Response(Interface& interface) {
        return interface.perf_response_small_;
    }
};

template <>
struct EventSelector<PerfPayloadSize::Medium> {
    template <typename Interface>
    static auto& Request(Interface& interface) {
        return interface.perf_request_medium_;
    }
    template <typename Interface>
    static auto& Response(Interface& interface) {
        return interface.perf_response_medium_;
    }
};

/// Both processes run on the same host, so a monotonic clock is directly comparable and no clock
/// offset correction is needed for the one-way latency.
inline std::uint64_t GetCurrentTimeNanos() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

inline void FillTestPayload(std::uint8_t* payload, std::uint32_t size) {
    for (std::uint32_t i{0}; i < size; ++i) {
        payload[i] = static_cast<std::uint8_t>(0xA5 + (i & 0xFF));
    }
}

inline bool VerifyTestPayload(const std::uint8_t* payload, std::uint32_t size) {
    for (std::uint32_t i{0}; i < size; ++i) {
        if (payload[i] != static_cast<std::uint8_t>(0xA5 + (i & 0xFF))) {
            return false;
        }
    }
    return true;
}

template <PerfPayloadSize PayloadBytes>
void WriteMessage(PerfSample<PayloadBytes>& sample, const PerfMessage<PayloadBytes>& message) {
    static_assert(sizeof(message) <= PerfSample<PayloadBytes>::kMaxMessageSize);
    std::memcpy(sample.data, &message, sizeof(message));
    sample.size = sizeof(message);
}

template <PerfPayloadSize PayloadBytes>
bool ReadMessage(const PerfSample<PayloadBytes>& sample, PerfMessage<PayloadBytes>& message) {
    if (sample.size != sizeof(message)) {
        return false;
    }
    std::memcpy(&message, sample.data, sizeof(message));
    return true;
}

inline bool ParsePayloadSize(std::string_view name, PerfPayloadSize& out) {
    if (name == "tiny") {
        out = PerfPayloadSize::Tiny;
    } else if (name == "small") {
        out = PerfPayloadSize::Small;
    } else if (name == "medium") {
        out = PerfPayloadSize::Medium;
    } else {
        return false;
    }
    return true;
}

inline const char* PayloadSizeName(PerfPayloadSize size) {
    switch (size) {
        case PerfPayloadSize::Tiny:
            return "tiny";
        case PerfPayloadSize::Small:
            return "small";
        case PerfPayloadSize::Medium:
            return "medium";
    }
    return "unknown";
}

}  // namespace perf_service

#endif  // TESTS_E2E_PERF_PERF_SERVICE_H
