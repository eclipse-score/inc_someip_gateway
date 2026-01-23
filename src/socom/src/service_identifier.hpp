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

#ifndef SCORE_SOCOM_SERVICE_IDENTIFIER_HPP
#define SCORE_SOCOM_SERVICE_IDENTIFIER_HPP

#include "score/socom/service_interface.hpp"

namespace score {
namespace socom {

/// Service instance identification information
struct Service_identifier final {
    Service_interface interface;
    Service_instance instance;
};

/// \cond
bool operator<(Service_identifier const& lhs, Service_identifier const& rhs);
/// \endcond

}  // namespace socom
}  // namespace score

#endif  // SCORE_SOCOM_SERVICE_IDENTIFIER_HPP
