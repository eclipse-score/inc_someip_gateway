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

#include "src/someipd/mwcom_adapter.h"

#include <algorithm>
#include <cstring>

#include "score/mw/log/logging.h"

namespace score::someip_gateway::someipd {

using score::someip_gateway::network_service::interfaces::message_transfer::MAX_MESSAGE_SIZE;

MwcomAdapter::MwcomAdapter(std::string proxy_instance_specifier,
                           std::string skeleton_instance_specifier, std::size_t max_sample_count)
    : proxy_instance_specifier_(std::move(proxy_instance_specifier)),
      skeleton_instance_specifier_(std::move(skeleton_instance_specifier)),
      max_sample_count_(max_sample_count) {}

bool MwcomAdapter::Init(int argc, const char* argv[]) {
    score::mw::com::runtime::InitializeRuntime(argc, argv);

    auto handles = Proxy::FindService(
                       score::mw::com::InstanceSpecifier::Create(proxy_instance_specifier_).value())
                       .value();
    proxy_.emplace(Proxy::Create(handles.front()).value());
    proxy_->message_.Subscribe(max_sample_count_);

    auto create_result = Skeleton::Create(
        score::mw::com::InstanceSpecifier::Create(skeleton_instance_specifier_).value());
    skeleton_.emplace(std::move(create_result).value());
    (void)skeleton_->OfferService();

    return true;
}

bool MwcomAdapter::SendToGatewayd(const std::byte* data, std::size_t size) {
    auto maybe_message = skeleton_->message_.Allocate();
    if (!maybe_message.has_value()) {
        score::mw::log::LogError()
            << "Failed to allocate SOME/IP message: " << maybe_message.error().Message();
        return false;
    }
    auto message_sample = std::move(maybe_message).value();
    const auto copy_size = std::min(size, MAX_MESSAGE_SIZE);
    std::memcpy(message_sample->data, data, copy_size);
    message_sample->size = size;
    skeleton_->message_.Send(std::move(message_sample));
    return true;
}

void MwcomAdapter::ReceiveFromGatewayd(MessageCallback callback, std::size_t max_count) {
    proxy_->message_.GetNewSamples(
        [&callback](auto sample) { callback(sample->data, sample->size); }, max_count);
}

}  // namespace score::someip_gateway::someipd
