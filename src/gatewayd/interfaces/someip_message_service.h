/********************************************************************************
 * Copyright (c) 2025 ETAS GmbH
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

#ifndef SRC_GATEWAYD_INTERFACES_SOMEIP_MESSAGE_SERVICE
#define SRC_GATEWAYD_INTERFACES_SOMEIP_MESSAGE_SERVICE

#include <score/mw/com/types.h>

#include <cstddef>

/// Service for exchanging raw SOME/IP messages.
/// Used between gatewayd and someipd for the payload communication.
namespace someip_message_service {
constexpr std::size_t MAX_MESSAGE_SIZE = 1500U;  // TODO: Make configurable

struct SomeipMessage {
    std::size_t size{};
    std::byte data[MAX_MESSAGE_SIZE];
};

template <typename Trait>
class SomeipMessageService : public Trait::Base {
   public:
    using Trait::Base::Base;

    /// Sends the given SOME/IP message.
    typename Trait::template Event<SomeipMessage> message_{*this, "message"};
};

using SomeipMessageServiceProxy = score::mw::com::AsProxy<SomeipMessageService>;
using SomeipMessageServiceSkeleton = score::mw::com::AsSkeleton<SomeipMessageService>;

}  // namespace someip_message_service

#endif  // SRC_GATEWAYD_INTERFACES_SOMEIP_MESSAGE_SERVICE
