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

    OfferEvents();
    OfferFields();
    RegisterMethodHandler();

    // offer_service MUST be called after all offer_event() calls and BEFORE SeedInitialValues().
    // REQ: comp_req__tc8_conformance__sd_offer_format
    app_->offer_service(config_.service_id, config_.instance_id,
                        config_.major_version, config_.minor_version);

    SeedInitialValues();
    StartNotifyThread();

    score::mw::log::LogInfo()
        << "[tc8_dut] Offering service 0x" << score::mw::log::LogHex16{config_.service_id}
        << "/0x" << score::mw::log::LogHex16{config_.instance_id};
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

        // Fields are always ET_FIELD: vsomeip delivers the cached payload to each new
        // subscriber on subscribe-ACK without waiting for the next notify() cycle.
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
    // One handler for all methods, dispatching by method ID at runtime.
    // REQ: comp_req__tc8_conformance__msg_resp_header
    app_->register_message_handler(
        config_.service_id, config_.instance_id, vsomeip::ANY_METHOD,
        [this](const std::shared_ptr<vsomeip::message>& request) {
            HandleMethod(request);
        });
}

void DutService::SeedInitialValues() {
    for (const EventConfig& ev : config_.events) {
        if (ev.initial_value_size == 0U) {
            continue;
        }
        auto payload = vsomeip::runtime::get()->create_payload();
        payload->set_data(ev.initial_value.data(),
                          static_cast<vsomeip::length_t>(ev.initial_value_size));
        // Passing false for force: seeds the cache without triggering a cyclic notification.
        app_->notify(config_.service_id, config_.instance_id, ev.id, payload, false);
    }

    for (std::size_t i = 0U; i < config_.fields.size() && i < kMaxFields; ++i) {
        const FieldState& fs = field_states_[i];
        if (fs.size == 0U) {
            continue;
        }
        auto payload = vsomeip::runtime::get()->create_payload();
        payload->set_data(fs.data.data(), static_cast<vsomeip::length_t>(fs.size));
        app_->notify(config_.service_id, config_.instance_id, config_.fields[i].notify_id,
                     payload, false);
    }
}

void DutService::StartNotifyThread() {
    running_.store(true);
    notify_thread_ = std::thread(&DutService::NotifyLoop, this);
}

void DutService::NotifyLoop() {
    // 500ms tick: the GCD of all cycle_ms values in the ETS spec.
    // Events with a nonzero cycle_ms are notified when elapsed_ms is a multiple of cycle_ms.
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
            // Passing true for force: ET_FIELD events require it as vsomeip silently drops
            // unchanged payloads. TC8-RPC-15 requires cyclic sends.
            // REQ: comp_req__tc8_conformance__fld_initial_value
            app_->notify(config_.service_id, config_.instance_id, ev.id, payload, true);
        }

        for (std::size_t i = 0U; i < config_.fields.size() && i < kMaxFields; ++i) {
            // Fields have no cycle_ms: they are driven by SET commands.
            // This loop is a placeholder for future extension.
            (void)i;
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
