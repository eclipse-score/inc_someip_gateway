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

#include <algorithm>
#include <cassert>
#include <mutex>

#include "runtime_impl.hpp"

namespace score {
namespace socom {

void Runtime_impl::update_bridges_provided_services(Bridge_registration_id const& bridge_id,
                                                    Service_interface_identifier const& interface,
                                                    Service_instance const& instance,
                                                    Find_result_status status) {
    std::lock_guard<std::mutex> const lock{m_bridge_mutex};
    auto const bridge_to_callbacks = m_bridge_to_callbacks.find(bridge_id);

    // If statement expression may evaluate to true because Runtime_impl implements
    // Stop_registration where map element with bridge_id is removed. For that reason further access
    // to m_bridge_to_callbacks map element makes no sense. An early return takes place.
    //
    // Sporadically in TEST_F(RuntimeMultiThreadingTest,
    // BridgesAndSubscribeFindServiceHaveNoRaceConditions) where tight loops of
    // register_service_bridge are made, could be observed early return takes on place because of
    // thread aware, concurrent API calls. Code is excluded from Bullseye coverage to prevent false
    // reports.

    if (std::end(m_bridge_to_callbacks) == bridge_to_callbacks) {
        return;
    }

    auto& available_services = std::get<2>(bridge_to_callbacks->second);
    if (Find_result_status::added == status) {
        available_services[interface].emplace_back(instance);
    } else {
        auto const if_iter = available_services.find(interface);
        if (std::end(available_services) == if_iter) {
            return;
        }
        Instances& instances = if_iter->second;
        auto const end =
            std::remove_if(std::begin(instances), std::end(instances),
                           [&instance](Service_instance const& elem) { return instance == elem; });
        instances.erase(end, std::end(instances));
        cleanup(available_services, interface, instances);
    }
}

}  // namespace socom
}  // namespace score
