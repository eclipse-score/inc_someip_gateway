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
#ifndef SCORE_MW_IPC_BRIDGE_ASSERTHANDLER_H
#define SCORE_MW_IPC_BRIDGE_ASSERTHANDLER_H

namespace score::gateway {

/// \brief Sets up the assertion handler for mw::com assertions to print error messages for failed assertions
void SetupAssertHandler();

} // namespace score::gateway

#endif // SCORE_MW_IPC_BRIDGE_ASSERTHANDLER_H
