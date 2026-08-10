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

#include "dut_service.h"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <set>

#include "score/mw/log/logging.h"

namespace score::someipd_tc8 {

DutService::DutService(std::shared_ptr<vsomeip::application> app, const DutConfig& config)
    : app_(std::move(app)), config_(config) {}

DutService::~DutService() {
    // Guard against use without calling Stop() first.
    if (running_.load()) {
        Stop();
    }
}

void DutService::Start() {
    for (std::size_t i = 0U; i < config_.fields.size() && i < kMaxFields; ++i) {
        const FieldConfig& fc = config_.fields[i];
        field_states_[i].size = fc.initial_value_size;
        std::memcpy(field_states_[i].data.data(), fc.initial_value.data(), fc.initial_value_size);
    }

    // Defer all vsomeip API calls to the ST_REGISTERED callback so they execute
    // while the io_context is running (during app_->start()). This ensures is_set_
    // is committed to the event cache in the correct runtime state, which is required
    // for send_initial_events() to deliver the cached field value to new subscribers.
    // Calling offer_event(), offer_service(), and notify() before app_->start() causes
    // vsomeip's startup sequence to reset event state, preventing initial notification.
    app_->register_state_handler([this](vsomeip::state_type_e state) {
        if (state != vsomeip::state_type_e::ST_REGISTERED) {
            return;
        }
        OfferEvents();
        OfferFields();
        RegisterMethodHandler();
        // REQ: comp_req__tc8_conformance__sd_offer_format
        app_->offer_service(config_.service_id, config_.instance_id,
                            config_.major_version, config_.minor_version);
        score::mw::log::LogInfo()
            << "[tc8_dut] Offering service 0x"
            << score::mw::log::LogHex16{config_.service_id}
            << "/0x" << score::mw::log::LogHex16{config_.instance_id};
        SeedInitialValues();
        RegisterSubscriptionHandlers();
        StartNotifyThread();
    });
}

void DutService::Stop() {
    running_.store(false);
    if (notify_thread_.joinable()) {
        notify_thread_.join();
    }
    app_->stop_offer_service(config_.service_id, config_.instance_id);
    score::mw::log::LogInfo() << "[tc8_dut] Service stopped.";
}

void DutService::OfferEvents() {
    for (const EventConfig& ev : config_.events) {
        std::set<vsomeip::eventgroup_t> groups;
        for (uint8_t i = 0U; i < ev.eventgroup_count; ++i) {
            groups.insert(ev.eventgroups[i]);
        }

        const vsomeip::event_type_e ev_type =
            ev.is_field ? vsomeip::event_type_e::ET_FIELD : vsomeip::event_type_e::ET_EVENT;
        const vsomeip::reliability_type_e reliability =
            ev.reliable ? vsomeip::reliability_type_e::RT_RELIABLE
                        : vsomeip::reliability_type_e::RT_UNRELIABLE;

        app_->offer_event(config_.service_id, config_.instance_id, ev.id, groups, ev_type,
                          std::chrono::milliseconds::zero(),
                          /*_change_resets_cycle=*/false,
                          /*_update_on_change=*/true,
                          /*_epsilon_change_func=*/nullptr, reliability);

        score::mw::log::LogInfo()
            << "[tc8_dut] Offered event 0x" << score::mw::log::LogHex16{ev.id}
            << " (" << ev.name << ") reliable=" << ev.reliable << " is_field=" << ev.is_field;
    }
}

void DutService::OfferFields() {
    for (const FieldConfig& fld : config_.fields) {
        std::set<vsomeip::eventgroup_t> groups;
        for (uint8_t i = 0U; i < fld.eventgroup_count; ++i) {
            groups.insert(fld.eventgroups[i]);
        }

        const vsomeip::reliability_type_e reliability =
            fld.reliable ? vsomeip::reliability_type_e::RT_RELIABLE
                         : vsomeip::reliability_type_e::RT_UNRELIABLE;

        // REQ: comp_req__tc8_conformance__fld_initial_value
        app_->offer_event(config_.service_id, config_.instance_id, fld.notify_id, groups,
                          vsomeip::event_type_e::ET_FIELD,
                          std::chrono::milliseconds::zero(),
                          /*_change_resets_cycle=*/false,
                          /*_update_on_change=*/true,
                          /*_epsilon_change_func=*/nullptr, reliability);

        score::mw::log::LogInfo()
            << "[tc8_dut] Offered field notify 0x"
            << score::mw::log::LogHex16{fld.notify_id}
            << " (" << fld.name << ") reliable=" << fld.reliable;
    }
}

void DutService::RegisterMethodHandler() {
    // REQ: comp_req__tc8_conformance__msg_resp_header
    app_->register_message_handler(
        config_.service_id, config_.instance_id, vsomeip::ANY_METHOD,
        [this](const std::shared_ptr<vsomeip::message>& request) {
            HandleMethod(request);
        });
}

void DutService::SeedInitialValues() {
    score::mw::log::LogInfo() << "[tc8_dut] SeedInitialValues called";
    for (std::size_t i = 0U; i < config_.fields.size() && i < kMaxFields; ++i) {
        const FieldState& fs = field_states_[i];
        if (fs.size == 0U) {
            continue;
        }
        auto payload = vsomeip::runtime::get()->create_payload();
        payload->set_data(fs.data.data(), static_cast<vsomeip::length_t>(fs.size));
        app_->notify(config_.service_id, config_.instance_id, config_.fields[i].notify_id,
                     payload, true);
        score::mw::log::LogInfo()
            << "[tc8_dut] notify field 0x"
            << score::mw::log::LogHex16{config_.fields[i].notify_id};
    }

    for (const EventConfig& ev : config_.events) {
        if (!ev.is_field || ev.initial_value_size == 0U) {
            continue;
        }
        auto payload = vsomeip::runtime::get()->create_payload();
        payload->set_data(ev.initial_value.data(),
                          static_cast<vsomeip::length_t>(ev.initial_value_size));
        app_->notify(config_.service_id, config_.instance_id, ev.id, payload, true);
        score::mw::log::LogInfo()
            << "[tc8_dut] notify event (field) 0x"
            << score::mw::log::LogHex16{ev.id};
    }
}

void DutService::RegisterSubscriptionHandlers() {
    // Collect unique eventgroup IDs. vsomeip replaces the previous handler when
    // register_subscription_handler() is called with the same (service, instance,
    // eventgroup) key, so we register exactly one handler per eventgroup.
    std::set<vsomeip::eventgroup_t> eventgroups;
    for (std::size_t i = 0U; i < config_.fields.size() && i < kMaxFields; ++i) {
        const FieldConfig& fld = config_.fields[i];
        for (uint8_t j = 0U; j < fld.eventgroup_count; ++j) {
            eventgroups.insert(fld.eventgroups[j]);
        }
    }

    for (const vsomeip::eventgroup_t eg : eventgroups) {

        app_->register_subscription_handler(
            config_.service_id, config_.instance_id, eg,
            [eg](vsomeip::client_t /*client*/,
                 const vsomeip_sec_client_t* /*sec_client*/,
                 const std::string& /*env*/,
                 bool subscribed) -> bool {
                if (!subscribed) {
                    score::mw::log::LogInfo()
                        << "[tc8_dut] subscriber 0x" << score::mw::log::LogHex16{eg}
                        << " unsubscribed";
                    return true;
                }
                score::mw::log::LogInfo()
                    << "[tc8_dut] subscriber 0x" << score::mw::log::LogHex16{eg}
                    << " subscribed";
                // vsomeip calls send_initial_events() after this handler returns true,
                // which delivers the cached field value to the new subscriber automatically.
                return true;
            });
    }
}

void DutService::StartNotifyThread() {
    running_.store(true);
    notify_thread_ = std::thread(&DutService::NotifyLoop, this);
}

void DutService::NotifyLoop() {
    uint32_t elapsed_ms = 0U;
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500U));
        elapsed_ms += 500U;

        // Notify cyclic events.
        for (const EventConfig& ev : config_.events) {
            if (ev.cycle_ms == 0U) {
                continue;  // Seed-only: no periodic notification.
            }
            if ((elapsed_ms % ev.cycle_ms) != 0U) {
                continue;
            }
            auto payload = vsomeip::runtime::get()->create_payload();
            payload->set_data(ev.initial_value.data(),
                              static_cast<vsomeip::length_t>(ev.initial_value_size));
            // REQ: comp_req__tc8_conformance__fld_initial_value
            app_->notify(config_.service_id, config_.instance_id, ev.id, payload, true);
        }
    }
}

void DutService::HandleMethod(const std::shared_ptr<vsomeip::message>& request) {
    // Fire-and-forget: do not send a response.
    // REQ: comp_req__tc8_conformance__msg_resp_header
    if (request->get_message_type() == vsomeip::message_type_e::MT_REQUEST_NO_RETURN) {
        const vsomeip::method_t method = request->get_method();

        // resetInterface: re-seed all fields to their initial values.
        for (const MethodConfig& mc : config_.methods) {
            if (mc.id == method && mc.name == "resetInterface") {
                SeedInitialValues();
                score::mw::log::LogInfo() << "[tc8_dut] resetInterface: re-seeded fields";
                break;
            }
        }
        return;
    }

    const vsomeip::method_t method = request->get_method();
    auto response = vsomeip::runtime::get()->create_response(request);

    // Field getter and setter dispatch.
    for (std::size_t i = 0U; i < config_.fields.size() && i < kMaxFields; ++i) {
        const FieldConfig& fld = config_.fields[i];

        if (method == fld.getter_method_id) {
            // REQ: comp_req__tc8_conformance__fld_get_set
            auto resp_payload = vsomeip::runtime::get()->create_payload();
            {
                std::lock_guard<std::mutex> lock(field_mutex_);
                resp_payload->set_data(field_states_[i].data.data(),
                                       static_cast<vsomeip::length_t>(field_states_[i].size));
            }
            response->set_payload(resp_payload);
            app_->send(response);
            return;
        }

        if (fld.setter_method_id.has_value() && method == fld.setter_method_id.value()) {
            // REQ: comp_req__tc8_conformance__fld_get_set
            auto req_payload = request->get_payload();
            const vsomeip::length_t req_len = req_payload->get_length();
            if (req_len <= static_cast<vsomeip::length_t>(kMaxInitialValueBytes)) {
                {
                    std::lock_guard<std::mutex> lock(field_mutex_);
                    field_states_[i].size = static_cast<std::size_t>(req_len);
                    std::memcpy(field_states_[i].data.data(), req_payload->get_data(), req_len);
                }
                // Notify subscribers of the new field value.
                auto notify_payload = vsomeip::runtime::get()->create_payload();
                notify_payload->set_data(req_payload->get_data(), req_len);
                app_->notify(config_.service_id, config_.instance_id, fld.notify_id,
                             notify_payload, true);
            }
            // Empty payload response means success (E_OK is implicit).
            response->set_return_code(vsomeip::return_code_e::E_OK);
            app_->send(response);
            return;
        }
    }

    // Echo methods: non-fire-and-forget methods not matched above echo the request payload.
    for (const MethodConfig& mc : config_.methods) {
        if (mc.id == method && !mc.fire_and_forget) {
            // Echo: copy payload unchanged.
            response->set_payload(request->get_payload());
            app_->send(response);
            return;
        }
    }

    // Unknown method: reply with E_UNKNOWN_METHOD.
    // REQ: comp_req__tc8_conformance__msg_error_codes
    score::mw::log::LogWarn()
        << "[tc8_dut] Unknown method 0x" << score::mw::log::LogHex16{method}
        << " — sending E_UNKNOWN_METHOD";
    response->set_return_code(vsomeip::return_code_e::E_UNKNOWN_METHOD);
    app_->send(response);
}

}  // namespace score::someipd_tc8
