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
 * SPDX-License-Identifier: Apache-2.0 AND CC0-1.0
 * AI Disclosure: Modifications for issue #84 were generated with OpenAI Codex
 * (model revision unavailable). These AI-generated modifications are offered under
 * CC0-1.0; pre-existing content retains Apache-2.0. Human review is pending.
 ********************************************************************************/

#include "service_identifier.hpp"

#include <tuple>

namespace score::socom {

bool operator<(Service_registration_key const& lhs, Service_registration_key const& rhs) {
    return std::make_tuple(lhs.instance, lhs.interface.get_identifier()) <
           std::make_tuple(rhs.instance, rhs.interface.get_identifier());
}

}  // namespace score::socom
