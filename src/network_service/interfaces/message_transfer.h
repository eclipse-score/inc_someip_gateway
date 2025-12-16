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

#ifndef SRC_NETWORK_SERVICE_INTERFACES_MESSAGE_TRANSFER
#define SRC_NETWORK_SERVICE_INTERFACES_MESSAGE_TRANSFER

#include <cstddef>

#include "score/mw/com/types.h"

/// Service for exchanging raw SOME/IP messages.
/// Used between gatewayd and someipd for the payload communication.
namespace score::someip_gateway::network_service::interfaces::message_transfer {
constexpr std::size_t MAX_MESSAGE_SIZE = 1500U;  // TODO: Make configurable

struct SomeipMessage {
    std::size_t size{};
    std::byte data[MAX_MESSAGE_SIZE];
};

template <typename Trait>
class SomeipMessageTransferService : public Trait::Base {
   public:
    using Trait::Base::Base;

    /// Sends the given SOME/IP message.
    typename Trait::template Event<SomeipMessage> message_{*this, "message"};
};

using SomeipMessageTransferProxy = score::mw::com::AsProxy<SomeipMessageTransferService>;
using SomeipMessageTransferSkeleton = score::mw::com::AsSkeleton<SomeipMessageTransferService>;

}  // namespace score::someip_gateway::network_service::interfaces::message_transfer

#endif  // SRC_NETWORK_SERVICE_INTERFACES_MESSAGE_TRANSFER
