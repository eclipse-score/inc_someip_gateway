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

#ifndef SCORE_SOCOM_PAYLOAD_MOCK_HPP
#define SCORE_SOCOM_PAYLOAD_MOCK_HPP

#include <gmock/gmock.h>

#include <score/socom/payload.hpp>

namespace score::socom {

class Payload_mock : public Payload {
   public:
    MOCK_METHOD(Span, data, (), (const, noexcept, override));
    MOCK_METHOD(Span, header, (), (const, noexcept, override));
    MOCK_METHOD(Writable_span, header, (), (noexcept, override));
};

}  // namespace score::socom

#endif  // SCORE_SOCOM_PAYLOAD_MOCK_HPP
