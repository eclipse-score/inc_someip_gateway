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
#include <score/socom/vector_payload.hpp>

namespace score::socom {

namespace {

/// Payload implementation (example) based on a std::vector<> container
class Vector_payload final : public Payload {
    std::size_t m_lead_offset;
    std::size_t m_header_size;
    Vector_buffer m_buffer;

   public:
    Vector_payload(std::size_t const lead_offset, std::size_t const header_size,
                   Vector_buffer buffer)
        : m_lead_offset{lead_offset}, m_header_size{header_size}, m_buffer{std::move(buffer)} {}

    explicit Vector_payload(std::size_t const header_size, Vector_buffer buffer)
        : Vector_payload{0U, header_size, std::move(buffer)} {}

    Vector_payload(Vector_payload const&) = delete;
    Vector_payload(Vector_payload&&) = delete;
    Vector_payload& operator=(Vector_payload const&) = delete;
    Vector_payload& operator=(Vector_payload&&) = delete;
    ~Vector_payload() noexcept override = default;

    [[nodiscard]] Span data() const noexcept override {
        Span const span{m_buffer};
        std::size_t const num_bytes{m_buffer.size() - m_header_size - m_lead_offset};
        return span.last(static_cast<Span::size_type>(num_bytes));
    }

    [[nodiscard]] Writable_span header() noexcept override {
        Writable_span const span{m_buffer};
        return span.subspan(m_lead_offset, m_header_size);
    }

    [[nodiscard]] Span header() const noexcept override {
        Span const span{m_buffer};
        return span.subspan(m_lead_offset, m_header_size);
    }
};

}  // namespace

Payload::Sptr make_vector_payload(Vector_buffer buffer) {
    return std::make_shared<Vector_payload>(0, std::move(buffer));
}

Payload::Sptr make_vector_payload(std::size_t header_size, Vector_buffer buffer) {
    assert(header_size <= buffer.size());
    return std::make_shared<Vector_payload>(header_size, std::move(buffer));
}

Payload::Sptr make_vector_payload(std::size_t lead_offset, std::size_t header_size,
                                  Vector_buffer buffer) {
    assert((lead_offset + header_size) <= buffer.size());
    return std::make_shared<Vector_payload>(lead_offset, header_size, std::move(buffer));
}

}  // namespace score::socom
