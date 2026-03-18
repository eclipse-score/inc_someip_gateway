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

#include "src/someipd/vsomeip_adapter.h"

#include <iostream>
#include <set>

namespace score::someip_gateway::someipd {

VsomeipAdapter::VsomeipAdapter(std::string app_name) : app_name_(std::move(app_name)) {}

VsomeipAdapter::~VsomeipAdapter() { StopProcessing(); }

bool VsomeipAdapter::Init() {
    auto runtime = vsomeip::runtime::get();
    application_ = runtime->create_application(app_name_);
    if (!application_->init()) {
        return false;
    }
    payload_ = runtime->create_payload();
    return true;
}

void VsomeipAdapter::StartProcessing() {
    processing_thread_ = std::thread([this]() { application_->start(); });
}

void VsomeipAdapter::StopProcessing() {
    if (application_) {
        application_->stop();
    }
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
}

void VsomeipAdapter::OfferEvent(ServiceId service_id, InstanceId instance_id, EventId event_id,
                                EventGroupId eventgroup_id) {
    std::set<vsomeip::eventgroup_t> groups{eventgroup_id};
    application_->offer_event(service_id, instance_id, event_id, groups);
}

void VsomeipAdapter::OfferService(ServiceId service_id, InstanceId instance_id) {
    application_->offer_service(service_id, instance_id);
}

void VsomeipAdapter::Notify(ServiceId service_id, InstanceId instance_id, EventId event_id,
                            const std::byte* payload_data, std::size_t payload_size) {
    payload_->set_data(reinterpret_cast<const uint8_t*>(payload_data),
                       static_cast<uint32_t>(payload_size));
    application_->notify(service_id, instance_id, event_id, payload_);
}

void VsomeipAdapter::RequestService(ServiceId service_id, InstanceId instance_id) {
    application_->request_service(service_id, instance_id);
}

void VsomeipAdapter::SubscribeEvent(ServiceId service_id, InstanceId instance_id, EventId event_id,
                                    EventGroupId eventgroup_id) {
    std::set<vsomeip::eventgroup_t> groups{eventgroup_id};
    application_->request_event(service_id, instance_id, event_id, groups,
                                vsomeip::event_type_e::ET_EVENT);
    application_->subscribe(service_id, instance_id, eventgroup_id);
}

void VsomeipAdapter::RegisterMessageHandler(ServiceId service_id, InstanceId instance_id,
                                            EventId event_id, MessageHandler handler) {
    application_->register_message_handler(
        service_id, instance_id, event_id, [handler](const std::shared_ptr<vsomeip::message>& msg) {
            handler(msg->get_service(), msg->get_instance(), msg->get_method(),
                    reinterpret_cast<const std::byte*>(msg->get_payload()->get_data()),
                    msg->get_payload()->get_length());
        });
}

}  // namespace score::someip_gateway::someipd
