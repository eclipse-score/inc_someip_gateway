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

#ifndef SCORE_SOCOM_PAYLOAD_HPP
#define SCORE_SOCOM_PAYLOAD_HPP

#include <cstddef>
#include <memory>
#include <score/span.hpp>

namespace score::socom {

/// \brief Interface representing the Payload transferable by SOCom.
/// \details The payload itself must be representable by a continuous Span of bytes.
///
/// The Payload has an optional header(), which is writable, but is not part of the data
/// returned by span(). The optional header() is part of the same internal buffer, which also
/// backs span().
///
/// The payload can internally look as follows:
/// xxxxxxx SOME/IP_header | payload_data
///
/// Here | shows the position of the actual payload start in the buffer. Here "payload_data"
/// will be returned with span().
///
/// This is needed for algorithms like the one for E2E, which require all data
/// to be in contiguous memory and require an additional header for processing.
/// \note When sending data over the wire, only data returned by span() shall be sent.
class Payload {
   public:
    /// \brief Alias for a shared pointer to this interface.
    using Sptr = std::shared_ptr<Payload>;

    /// \brief Alias for a data byte.
    using Byte = std::byte;

    /// \brief Alias for payload data.
    using Span = score::cpp::span<Byte const>;

    /// \brief Alias for writable payload data.
    using Writable_span = score::cpp::span<Byte>;

    Payload() = default;
    virtual ~Payload() = default;
    Payload(Payload const&) = delete;
    Payload(Payload&&) = delete;
    Payload& operator=(Payload const&) = delete;
    Payload& operator=(Payload&&) = delete;

    /// \brief Retrieves the payload data.
    /// \return Span of payload data.
    [[nodiscard]] virtual Span data() const noexcept = 0;

    /// \brief Retrieves the header data.
    /// \return Span of header data.
    [[nodiscard]] virtual Span header() const noexcept = 0;

    /// \brief Retrieves the header data.
    /// \return Writable span of header data.
    [[nodiscard]] virtual Writable_span header() noexcept = 0;
};

/// \brief An empty payload instance, which may be used as default value for the payload parameter.
/// \return A pointer to a Payload object.
extern Payload::Sptr empty_payload();

/// \brief Operator == for Payload.
/// \param lhs Left-hand side of operator.
/// \param rhs Right-hand side of operator.
/// \return True in case of equality, otherwise false.
bool operator==(Payload const& lhs, Payload const& rhs);

/// \brief Operator != for Payload.
/// \param lhs Left-hand side of operator.
/// \param rhs Right-hand side of operator.
/// \return True in case of inequality, otherwise false.
bool operator!=(Payload const& lhs, Payload const& rhs);

/// \brief Interface representing a writable payload, which can be allocated by the recipient for
/// zero copy operations.
///
/// The recipient is responsible for allocating enough data for the sender.
class Writable_payload : public Payload {
   public:
    /// \brief Alias for a shared pointer to this interface.
    using Sptr = std::shared_ptr<Writable_payload>;
    /// \brief Alias for a unique pointer to this interface.
    using Uptr = std::unique_ptr<Writable_payload>;

    /// \brief Retrieves the writable payload data.
    /// \return Span of payload data.
    [[nodiscard]] virtual Writable_span wdata() noexcept = 0;
};

}  // namespace score::socom

#endif  // SCORE_SOCOM_PAYLOAD_HPP
