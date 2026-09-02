// *******************************************************************************
// Copyright (c) 2026 Contributors to the Eclipse Foundation
//
// See the NOTICE file(s) distributed with this work for additional
// information regarding copyright ownership.
//
// This program and the accompanying materials are made available under the
// terms of the Apache License Version 2.0 which is available at
// https://www.apache.org/licenses/LICENSE-2.0
//
// SPDX-License-Identifier: Apache-2.0
// *******************************************************************************

#ifndef TESTS_TC8_CONFORMANCE_APPLICATION_TC8_ETS_SERVICE_H
#define TESTS_TC8_CONFORMANCE_APPLICATION_TC8_ETS_SERVICE_H

#include <array>
#include <cstdint>

#include "score/mw/com/types.h"
#include "score/serializer/pre_serialized_data.h"

namespace tc8_ets_service {

// Maximum raw payload carried inside each PreSerializedData slot (1 byte for TC8 uint8 events).
inline constexpr std::size_t kMaxPayloadBytes{1U};

// SHM slot type metadata: sized and aligned for PreSerializedData<kMaxPayloadBytes>
// so that the NullSerializer can interpret each slot without data-format mismatch.
inline constexpr score::mw::com::DataTypeMetaInfo kDataTypeMetaInfo{
    score::someip_gateway::serializer::get_size_of_pre_serialized_data(kMaxPayloadBytes),
    alignof(score::someip_gateway::serializer::PreSerializedData<0>)};

inline constexpr std::array<score::mw::com::EventInfo, 9> kEvents = {{
    {"TestEventUINT8", kDataTypeMetaInfo},
    {"TestEventUINT8Array", kDataTypeMetaInfo},
    {"TestEventUINT8Reliable", kDataTypeMetaInfo},
    {"TestEventUINT8E2E", kDataTypeMetaInfo},
    {"InterfaceVersion", kDataTypeMetaInfo},
    {"TestFieldUINT8", kDataTypeMetaInfo},
    {"TestFieldUINT8Array", kDataTypeMetaInfo},
    {"TestFieldUINT8Reliable", kDataTypeMetaInfo},
    {"TestEventUINT8Multicast", kDataTypeMetaInfo},
}};

}  // namespace tc8_ets_service

#endif  // TESTS_TC8_CONFORMANCE_APPLICATION_TC8_ETS_SERVICE_H
