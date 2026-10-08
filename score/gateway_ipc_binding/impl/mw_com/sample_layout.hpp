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

#ifndef SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SAMPLE_LAYOUT_HPP
#define SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SAMPLE_LAYOUT_HPP

#include <cstddef>
#include <cstdint>
#include <limits>

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/mw/com/types.h"

namespace score::gateway_ipc_binding::mw_com {

/// \brief Layout of one `mw::com` event sample exchanged by this binding.
///
/// \details See `docs/mw_com_binding.rst`, section "Event sample layout":
///
///     +----------------------+-----------------------+------------------------+
///     | binding prefix (8 B) | reserved header space | payload bytes          |
///     |  u32 payload_length  |  header_size          |  max_payload_size      |
///     |  u32 reserved        |                       |                        |
///     +----------------------+-----------------------+------------------------+
///     ^                      ^                       ^
///     sample base            Payload::header()       Payload::data()
///
///          `mw::com` samples are fixed size while SOME/IP payloads are not, so the actual
///          length of the payload travels in the prefix. The prefix is 8 bytes so that the
///          reserved header space stays 8-byte aligned.

/// \brief Largest payload length the 32 bit length prefix can express.
inline constexpr std::size_t kMax_payload_length =
    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());

/// \brief Number of bytes of a sample the binding actually addresses.
/// \details This is `kSample_prefix_size + header_size + max_payload_size`. It can be smaller
///          than `sample_size()`, which rounds up to `kSample_alignment` because a
///          `DataTypeMetaInfo::size` must be an integer multiple of its alignment. The padding
///          at the end is never addressed, so spans are built over this size and
///          `Payload::data().size()` is exactly `max_payload_size`.
/// \param event Event configuration to size
/// \return Number of addressed bytes
[[nodiscard]] std::size_t addressed_sample_size(Event_config const& event) noexcept;

/// \brief mw::com meta info describing one sample of the given event.
/// \param event Event configuration to describe
/// \return Size and alignment to pass to GenericSkeleton::Create()
[[nodiscard]] score::mw::com::DataTypeMetaInfo sample_meta_info(Event_config const& event) noexcept;

/// \brief Write the payload length into the binding prefix of a sample.
/// \param sample_base Pointer to the first byte of the sample, must not be nullptr
/// \param length Payload length in bytes, must not exceed kMax_payload_length
void write_payload_length(std::byte* sample_base, std::size_t length) noexcept;

/// \brief Read the payload length back from the binding prefix of a sample.
/// \param sample_base Pointer to the first byte of the sample, must not be nullptr
/// \return The payload length written by write_payload_length()
[[nodiscard]] std::size_t read_payload_length(std::byte const* sample_base) noexcept;

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_IMPL_MW_COM_SAMPLE_LAYOUT_HPP
