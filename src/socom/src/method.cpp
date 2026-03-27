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

#include "score/socom/method.hpp"

#include <cassert>

namespace score {
namespace socom {

Method_call_reply_data ::Method_call_reply_data(Method_reply_callback reply_callback,
                                                Writable_payload::Uptr reply_payload)
    : reply_callback(std::move(reply_callback)), reply_payload(std::move(reply_payload)) {
    assert(this->reply_callback);
}

}  // namespace socom
}  // namespace score
