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

#include "routing.h"

#include <cassert>
#include <functional>
#include <iostream>
#include <thread>

#include "score/mw/log/logging.h"
#include "score/someip/constants.h"
#include "score/someip/someip_error.h"

namespace score::someipd {

using score::someip::kAnyInstance;

Routing::Routing(std::shared_ptr<const score::mw_someip_config::Root> config) : config_(config) {}

Routing::Routing(Routing&&) noexcept = default;

Routing& Routing::operator=(Routing&& other) noexcept {
    if (this != &other) {
        assert(!processing_thread_.joinable());
        config_ = std::move(other.config_);
        application_ = std::move(other.application_);
        payload_ = std::move(other.payload_);
        processing_thread_ = std::move(other.processing_thread_);
    }
    return *this;
}

Result<Routing> Routing::Create(std::shared_ptr<const score::mw_someip_config::Root> config) {
    Routing routing(std::move(config));
    auto runtime = vsomeip::runtime::get();
    routing.application_ = runtime->create_application("someipd");
    if (!routing.application_->init()) {
        score::mw::log::LogFatal() << "[someipd] vsomeip application initialization failed";
        return MakeUnexpected(score::someip::Errc::kInitializationFailed);
    }
    routing.payload_ = runtime->create_payload();
    std::cout << "[someipd] vsomeip application initialized successfully" << std::endl;

    return Result<Routing>{std::move(routing)};
}

void Routing::SetupOfferings() {
    for (const auto service_type : *config_->service_types()) {
        auto service_instances = service_type->local_service_instances();
        if (service_instances && service_instances->size() > 0) {
            for (const auto local_service_instance : *service_type->local_service_instances()) {
                for (const auto event : *service_type->events()) {
                    // TODO: Do Eventgroup handling. Currently just create one group for each event
                    // with the same ID as the event
                    std::set<vsomeip::eventgroup_t> groups{event->event_id()};
                    application_->offer_event(service_type->service_id(),
                                              local_service_instance->instance_id(),
                                              event->event_id(), groups);
                }
                std::cout << "[someipd] Offering service 0x" << std::hex
                          << service_type->service_id() << " instance 0x"
                          << local_service_instance->instance_id() << std::dec << std::endl;
                application_->offer_service(service_type->service_id(),
                                            local_service_instance->instance_id());
            }
        }
    }
}

InstanceId Routing::LookupInstanceId(ServiceId service_id) const {
    for (const auto service_type : *config_->service_types()) {
        if (service_type->service_id() != service_id) {
            continue;
        }
        return service_type->local_service_instances()->Get(0)->instance_id();
    }
    return kAnyInstance;
}

void Routing::ProcessMessages(std::atomic<bool>& shutdown_requested) {
    // TODO: Replace sleep loop with SOCom client_connector callbacks
    while (!shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "[someipd] Message loop exited, shutting down..." << std::endl;
}

void Routing::Run(std::atomic<bool>& shutdown_requested, std::function<void()> on_registered) {
    application_->register_state_handler(
        [this, on_registered = std::move(on_registered)](vsomeip::state_type_e state) {
            if (state == vsomeip::state_type_e::ST_REGISTERED) {
                std::cout << "[someipd] Application registered with routing daemon." << std::endl;
                std::cout << "[someipd] Setting up offerings..." << std::endl;
                SetupOfferings();
                if (on_registered) {
                    on_registered();
                }
            }
        });
    std::cout << "[someipd] Starting network stack processing..." << std::endl;
    processing_thread_ = std::thread([this]() { application_->start(); });
    std::cout << "[someipd] Network stack started, entering message loop." << std::endl;
    ProcessMessages(shutdown_requested);
    std::cout << "[someipd] Stopping network stack processing..." << std::endl;
    if (application_) {
        application_->stop();
    }
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
    std::cout << "[someipd] Network stack stopped." << std::endl;
}

}  // namespace score::someipd
