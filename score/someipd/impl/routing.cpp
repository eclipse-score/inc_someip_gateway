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

#include <functional>
#include <thread>
#include <utility>

#include "score/mw/log/logging.h"
#include "score/someip/someip_error.h"

namespace score::someipd {

Result<std::unique_ptr<Routing>> Routing::Create() {
    // Not make_unique: the constructor is private.
    std::unique_ptr<Routing> routing{new Routing{}};
    auto runtime = vsomeip::runtime::get();
    routing->application_ = runtime->create_application("someipd");
    if (!routing->application_->init()) {
        score::mw::log::LogFatal() << "[someipd] vsomeip application initialization failed";
        return MakeUnexpected(score::someip::Errc::kInitializationFailed);
    }
    routing->payload_ = runtime->create_payload();
    score::mw::log::LogInfo() << "[someipd] vsomeip application initialized successfully";

    return routing;
}

void Routing::SetupOfferings(score::mw_someip_config::Root const& config) {
    for (const auto* const service_type : *config.service_types()) {
        const auto* service_instances = service_type->local_service_instances();
        if ((service_instances != nullptr) && !service_instances->empty()) {
            for (const auto* const local_service_instance :
                 *service_type->local_service_instances()) {
                for (const auto* const event : *service_type->events()) {
                    std::set<vsomeip::eventgroup_t> groups;
                    auto const* eventgroup_ids = event->eventgroup_ids();
                    // Treat empty vector the same as absent: fall back to event_id as eventgroup.
                    if (eventgroup_ids != nullptr && !eventgroup_ids->empty()) {
                        for (auto group_id : *eventgroup_ids) {
                            groups.insert(group_id);
                        }
                    } else {
                        // Fallback: use event_id as group when no eventgroup_ids configured.
                        groups.insert(event->event_id());
                    }
                    application_->offer_event(service_type->service_id(),
                                              local_service_instance->instance_id(),
                                              event->event_id(), groups);
                }
                score::mw::log::LogInfo()
                    << "[someipd] Offering service 0x"
                    << score::mw::log::LogHex16{service_type->service_id()} << " instance 0x"
                    << score::mw::log::LogHex16{local_service_instance->instance_id()};
                application_->offer_service(service_type->service_id(),
                                            local_service_instance->instance_id(),
                                            service_type->service_version_major());
            }
        }
    }
}

void Routing::AddServices(std::shared_ptr<const score::mw_someip_config::Root> config,
                          std::function<void()> on_registered) {
    Services added{std::move(config), std::move(on_registered)};
    {
        std::lock_guard const lock{mutex_};
        services_.push_back(added);
        if (!registered_) {
            return;
        }
    }
    SetupOfferings(*added.config);
    added.on_registered();
}

void Routing::Run(const std::function<void()>& wait_for_shutdown) {
    application_->register_state_handler([this](vsomeip::state_type_e state) {
        std::vector<Services> services{};
        {
            std::lock_guard const lock{mutex_};
            registered_ = (state == vsomeip::state_type_e::ST_REGISTERED);
            if (!registered_) {
                return;
            }
            services = services_;
        }
        score::mw::log::LogInfo() << "[someipd] Application registered with routing daemon.";
        score::mw::log::LogInfo() << "[someipd] Setting up offerings...";
        for (auto const& added : services) {
            SetupOfferings(*added.config);
            added.on_registered();
        }
    });
    score::mw::log::LogInfo() << "[someipd] Starting network stack processing...";
    processing_thread_ = std::thread([this]() { application_->start(); });
    score::mw::log::LogInfo() << "[someipd] Network stack started, waiting for shutdown.";
    wait_for_shutdown();
    score::mw::log::LogInfo() << "[someipd] Stopping network stack processing...";
    if (application_) {
        application_->stop();
    }
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }

    score::mw::log::LogInfo() << "[someipd] Network stack stopped.";
}

}  // namespace score::someipd
