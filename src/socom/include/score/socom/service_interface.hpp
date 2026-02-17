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

#ifndef SCORE_SOCOM_SERVICE_INTERFACE_HPP
#define SCORE_SOCOM_SERVICE_INTERFACE_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>

namespace score::socom {

/// \brief Alias for a service instance.
using Service_instance = std::string;

/// \brief Service interface identification information.
struct Service_interface {
   public:
    /// \brief Alias for a service interface identifier.
    /// TODO sting registry optimization is missing
    using Id = std::string;

    /// \brief Service interface version type.
    struct Version {
        /// \brief Major version information.
        /// \note Major version must match exactly for service interface compatibility.
        std::uint16_t major;
        /// \brief Minor version information.
        /// \note Minor version of Client_connector is less or equal than the minor version of
        /// Server_connector for service interface compatibility.
        std::uint16_t minor;
    };

    /// \brief Service interface identifier.
    Id id;

    /// \brief Service interface version information.
    Version version;

    /// \brief Constructor.
    /// \param new_id ID of the service interface.
    /// \param new_version Version of the service interface.
    Service_interface(std::string_view new_id, Version new_version)
        : id{new_id}, version{new_version} {}
};

/// \brief Operator == for Service_interface::Version.
/// \param lhs Left-hand side of operator.
/// \param rhs Right-hand side of operator.
/// \return True in case of equality, otherwise false.
inline bool operator==(Service_interface::Version const& lhs,
                       Service_interface::Version const& rhs) {
    return (std::tie(lhs.major, lhs.minor) == std::tie(rhs.major, rhs.minor));
}

/// \brief Operator < for Service_interface::Version.
/// \param lhs Left-hand side of operator.
/// \param rhs Right-hand side of operator.
/// \return True in case the contents of lhs are lexicographically less than the contents of rhs,
/// otherwise false.
inline bool operator<(Service_interface::Version const& lhs,
                      Service_interface::Version const& rhs) {
    return (std::tie(lhs.major, lhs.minor) < std::tie(rhs.major, rhs.minor));
}

/// \brief Operator == for Service_interface.
/// \param lhs Left-hand side of operator.
/// \param rhs Right-hand side of operator.
/// \return True in case of equality, otherwise false.
inline bool operator==(Service_interface const& lhs, Service_interface const& rhs) {
    return (std::tie(lhs.id, lhs.version) == std::tie(rhs.id, rhs.version));
}

/// \brief Operator < for Service_interface.
/// \param lhs Left-hand side of operator.
/// \param rhs Right-hand side of operator.
/// \return True in case the contents of lhs are lexicographically less than the contents of rhs,
/// otherwise false.
inline bool operator<(Service_interface const& lhs, Service_interface const& rhs) {
    return (std::tie(lhs.id, lhs.version) < std::tie(rhs.id, rhs.version));
}

}  // namespace score::socom

#endif  // SCORE_SOCOM_SERVICE_INTERFACE_HPP
