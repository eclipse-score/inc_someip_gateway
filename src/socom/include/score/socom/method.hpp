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

#ifndef SCORE_SOCOM_METHOD_HPP
#define SCORE_SOCOM_METHOD_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <score/socom/error.hpp>
#include <score/socom/payload.hpp>
#include <variant>

namespace score::socom {
/// \brief Alias for a method ID.
using Method_id = std::uint16_t;

/// \brief A variadic template struct that implements the Visitor pattern.
/// \details The Visitor struct inherits from a parameter pack of types 'Ts' and aggregates their
/// operator() overloads. This allows for seamless type-specific operations on multiple types
/// without modifying their definitions.
/// \tparam Ts Types to be combined into the Visitor.
template <class... Ts>
struct Visitor : Ts... {
    using Ts::operator()...;
};

/// \brief A template deduction guide that simplifies the syntax for creating instances of Visitor
/// by eliminating the need to specify the template arguments.
/// \tparam Ts Types to be deduced from the constructor parameters.
template <class... Ts>
Visitor(Ts...) -> Visitor<Ts...>;

/// \brief Interface class for method call RAII type (see Client_connector::call_method).
class Method_invocation {
   public:
    /// \brief Alias for an unique pointer to this interface.
    using Uptr = std::unique_ptr<Method_invocation>;

    Method_invocation() = default;
    virtual ~Method_invocation() = default;

    Method_invocation(Method_invocation const&) = delete;
    Method_invocation(Method_invocation&&) = delete;

    Method_invocation& operator=(Method_invocation const&) = delete;
    Method_invocation& operator=(Method_invocation&&) = delete;
};

/// \brief Result of successful method call.
struct Application_return {
    /// \brief Constructor.
    /// \param p Payload data.
    explicit Application_return(Payload::Sptr p = empty_payload()) : payload{std::move(p)} {}

    /// \brief Payload data.
    Payload::Sptr payload;
};

/// \brief Result of failed method call.
struct Application_error {
    /// \brief Alias for an error code.
    using Code = std::int32_t;

    /// \brief Constructor.
    /// \param p Payload data.
    explicit Application_error(Payload::Sptr p = empty_payload()) : payload{std::move(p)} {}

    /// \brief Constructor.
    /// \param c Error code.
    /// \param p Payload data.
    explicit Application_error(Code c, Payload::Sptr p = empty_payload())
        : code{c}, payload{std::move(p)} {}

    /// \brief Error code.
    Code code{};

    /// \brief Payload data.
    Payload::Sptr payload;
};

/// \brief Alias for the response of a method.
using Method_result = std::variant<Application_return, Application_error, Error>;

/// \brief Operator == for Application_return.
/// \param lhs Left-hand side of operator.
/// \param rhs Right-hand side of operator.
/// \return True in case of equality, otherwise false.
inline bool operator==(Application_return const& lhs, Application_return const& rhs) {
    return *lhs.payload == *rhs.payload;
}

/// \brief Operator != for Application_return.
/// \param lhs Left-hand side of operator.
/// \param rhs Right-hand side of operator.
/// \return True in case of inequality, otherwise false.
inline bool operator!=(Application_return const& lhs, Application_return const& rhs) {
    return !(lhs == rhs);
}

/// \brief Operator == for Application_error.
/// \param lhs Left-hand side of operator.
/// \param rhs Right-hand side of operator.
/// \return True in case of equality, otherwise false.
inline bool operator==(Application_error const& lhs, Application_error const& rhs) {
    return (lhs.code == rhs.code) && (*lhs.payload == *rhs.payload);
}

/// \brief Operator != for Application_error.
/// \param lhs Left-hand side of operator.
/// \param rhs Right-hand side of operator.
/// \return True in case of inequality, otherwise false.
inline bool operator!=(Application_error const& lhs, Application_error const& rhs) {
    return !(lhs == rhs);
}

/// \brief Alias for the callback function of a method, in case a reply is requested.
using Method_reply_callback = std::function<void(Method_result const&)>;

/// \brief Callback and payload buffer for method call replies.
struct Method_call_reply_data {
    Method_reply_callback reply_callback;
    Writable_payload::Uptr reply_payload;

    Method_call_reply_data(Method_reply_callback reply_callback,
                           Writable_payload::Uptr reply_payload);
};

using Method_call_reply_data_opt = std::optional<Method_call_reply_data>;

}  // namespace score::socom

#endif  // SCORE_SOCOM_METHOD_HPP
