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
 * AI Disclosure: Assisted by OpenAI Codex (GPT-6.1 Sol).
 ********************************************************************************/

#ifndef SRC_SOCOM_SRC_SERVICE_IDENTIFIER
#define SRC_SOCOM_SRC_SERVICE_IDENTIFIER

#include "score/socom/service_interface_identifier.hpp"

namespace score::socom {

/// Internal duplicate-server registration key
///
/// This is only used to check if any (Disabled, Enabled) Server_connector for the given interface
/// and instance already exists.
///
/// For that check the interface is reduced to its service id and major version; the minor version
/// is a compatibility property of the instance and is intentionally not part of the identity (see
/// the operator< definition). This matches the canonical Service_database index.
// req-Id: comp_req__socom__registration_identity
struct Service_registration_key final {
    Service_interface interface;
    Service_instance instance;
};

/// \cond
bool operator<(Service_registration_key const& lhs, Service_registration_key const& rhs);
/// \endcond

}  // namespace score::socom

#endif  // SRC_SOCOM_SRC_SERVICE_IDENTIFIER
