/********************************************************************************
 * Copyright (c) 2025 Elektrobit Automotive GmbH
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

#include <score/socom/span.hpp>

#include <cstddef>
#include <memory>

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
    using Span = score::socom::span<Byte const>;

    /// \brief Alias for writable payload data.
    using Writable_span = score::socom::span<Byte>;

    Payload() = default;                         ///< Default constructor.
    virtual ~Payload() = default;                ///< Default destructor.
    Payload(Payload const&) = delete;            ///< Copy constructor cannot be used.
    Payload(Payload&&) = delete;                 ///< Move constructor cannot be used.
    Payload& operator=(Payload const&) = delete; ///< Copy assignment operator cannot be used.
    Payload& operator=(Payload&&) = delete;      ///< Move assignment operator cannot be used.

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

} // namespace score::socom

#endif // SCORE_SOCOM_PAYLOAD_HPP
