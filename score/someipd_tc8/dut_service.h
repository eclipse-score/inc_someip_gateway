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

#ifndef SCORE_SOMEIPD_TC8_DUT_SERVICE_H
#define SCORE_SOMEIPD_TC8_DUT_SERVICE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include <vsomeip/vsomeip.hpp>

#include "dut_config.h"

namespace score::someipd_tc8 {

/// Offers the TC8 DUT service via vsomeip, with events, fields, methods, and a periodic notify thread.
class DutService {
   public:
    /// @param app    Initialized vsomeip application (after init(), before start()).
    /// @param config Parsed and validated DutConfig.
    explicit DutService(std::shared_ptr<vsomeip::application> app, const DutConfig& config);

    ~DutService();

    // Non-copyable, non-movable: owns std::mutex and std::thread.
    DutService(const DutService&) = delete;
    DutService& operator=(const DutService&) = delete;
    DutService(DutService&&) = delete;
    DutService& operator=(DutService&&) = delete;

    /// Offer all events and fields, register the method handler, seed initial values,
    /// and start the periodic notify thread. Call before `app->start()`.
    void Start();

    /// Signal the notify thread to stop, join it, then call stop_offer_service.
    /// Call after `app->start()` returns.
    void Stop();

   private:
    void OfferEvents();
    void OfferFields();
    void RegisterMethodHandler();
    /// Call notify() for each event and field once to populate the vsomeip payload cache.
    /// Must be called after offer_service().
    // REQ: comp_req__tc8_conformance__fld_initial_value
    void SeedInitialValues();
    void StartNotifyThread();

    void NotifyLoop();

    // REQ: comp_req__tc8_conformance__msg_resp_header, comp_req__tc8_conformance__msg_error_codes
    void HandleMethod(const std::shared_ptr<vsomeip::message>& request);

    struct FieldState {
        std::array<vsomeip::byte_t, kMaxInitialValueBytes> data{};
        std::size_t size{0U};
    };

    std::shared_ptr<vsomeip::application> app_;
    DutConfig config_;

    /// One FieldState per entry in config_.fields, indexed by position.
    std::array<FieldState, kMaxFields> field_states_{};

    mutable std::mutex  field_mutex_;   // protects field_states_
    std::atomic<bool>   running_{false};
    std::thread         notify_thread_;
};

}  // namespace score::someipd_tc8

#endif  // SCORE_SOMEIPD_TC8_DUT_SERVICE_H
