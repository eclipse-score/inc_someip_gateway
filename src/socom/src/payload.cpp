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

#include <cassert>
#include <score/socom/payload.hpp>

namespace score::cpp {

template <typename T>
bool operator==(span<T> const& lhs, span<T> const& rhs) {
    return std::equal(std::begin(lhs), std::end(lhs), std::begin(rhs), std::end(rhs));
}
}  // namespace score::cpp

namespace score::socom {

bool operator==(Payload const& lhs, Payload const& rhs) {
    return (lhs.header() == rhs.header()) && (lhs.data() == rhs.data());
}

bool operator!=(Payload const& lhs, Payload const& rhs) { return !(lhs == rhs); }

Payload::Uptr empty_payload() {
    class Empty_payload final : public Payload {
       public:
        [[nodiscard]] Span data() const noexcept override { return {}; }
        [[nodiscard]] Writable_span header() noexcept override { return {}; }
        [[nodiscard]] Span header() const noexcept override { return {}; }
        [[nodiscard]] std::size_t get_slot_handle() const noexcept override {
            return kNoSlotHandle;
        }
    };

    return std::make_unique<Empty_payload>();
}

}  // namespace score::socom
