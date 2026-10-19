/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
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

#ifndef SCORE_SOCOM_FINAL_ACTION_HPP
#define SCORE_SOCOM_FINAL_ACTION_HPP

#include "score/callback.hpp"
#include "score/scope_exit/scope_exit.h"

namespace score {
namespace socom {

using Final_action = score::utils::ScopeExit<score::cpp::callback<void(), 128U>>;

}  // namespace socom
}  // namespace score

#endif  // SCORE_SOCOM_FINAL_ACTION_HPP
