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

#ifndef SCORE_SOCOM_RESULT_HPP
#define SCORE_SOCOM_RESULT_HPP

#include <score/expected.hpp>

namespace score::socom {
template <typename T, typename E>
using Expected = score::cpp::expected<T, E>;

using Blank = score::cpp::blank;
}  // namespace score::socom

#endif
