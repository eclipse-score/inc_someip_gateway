/********************************************************************************
 * Copyright (c) 2025 Elektrobit Automotive GmbH
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

#include "score/mw/com/types.h"
#include <score/gateway/common.h>
#include <score/gateway/datatype.h>
#include <score/gateway/payload_transformation_plugin_interface.hpp>
#include <score/gateway/plugin_handle.hpp>
#include <score/gateway/service_configuration.hpp>
#include <score/socom/runtime.hpp>

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <type_traits>

using score::gateway::IpcBridgeSkeleton;
using score::socom::Client_connector;
using score::socom::Event_mode;
using score::socom::Event_update_callback;
using score::socom::Service_instance;

namespace score::gateway {

namespace {
void copy_to(socom::Payload const& src, MapApiLanesStamped& dst)
{
    dst.string_data.fill('\0');
    auto const payload_data = src.data();
    assert(payload_data.size() <= dst.string_data.size());
    for (std::size_t i = 0U; i < payload_data.size(); ++i) {
        dst.string_data[i] = static_cast<char>(payload_data.data()[i]);
    }
}

template<typename T, typename E>
T unwrap(socom::Result<T, E> result)
{
    assert(result.has_value());
    return std::move(result).value();
}

void delete_plugin(Payload_transformation_plugin_interface* plugin)
{
    delete plugin;
}

Client_connector::Callbacks create_client_callbacks(IpcBridgeSkeleton& skeleton, std::size_t& cycle)
{
    Event_update_callback event_update_callback =
        [&skeleton, &cycle](auto const&, auto const /* event_id */, auto const& payload) {
            auto sample_result = PrepareMapLaneSample(skeleton, cycle);
            cycle += 1U;
            if (!sample_result.has_value()) {
                std::cerr << "Sample allocation failed. Exiting.\n";
                std::exit(EXIT_FAILURE);
            }
            auto sample = std::move(sample_result).value();
            copy_to(*payload, *sample);
            skeleton.map_api_lanes_stamped_.Send(std::move(sample));
        };
    Client_connector::Callbacks client_callbacks{
        [](auto const& client_connector, auto const service_state, auto const&) {
            if (socom::Service_state::available == service_state) {
                std::cout << "Service became available" << std::endl;
                auto const subscription_status =
                    client_connector.subscribe_event(0, Event_mode::update);
                assert(subscription_status);
            }
            else {
                std::cout << "Service became unavailable" << std::endl;
            }
        },
        event_update_callback, event_update_callback, [](auto const&, auto, auto) {}};
    return client_callbacks;
}

class Ipc_bridge_payload_transformator : public Payload_transformation_plugin_interface {
    IpcBridgeSkeleton skeleton_;
    std::size_t cycle_ = 0U;
    Client_connector::Callbacks client_callbacks_;
    socom::Client_connector::Uptr client_connector_;

public:
    Ipc_bridge_payload_transformator(IpcBridgeSkeleton skeleton, socom::Runtime& runtime)
        : skeleton_(std::move(skeleton))
        , client_callbacks_(create_client_callbacks(skeleton_, cycle_))
        , client_connector_(unwrap(runtime.make_client_connector(
              get_interface_configuration(), get_instance(), client_callbacks_)))
    {
        assert(client_connector_);
    }

    ~Ipc_bridge_payload_transformator() override
    {
        std::cout << "Stop offering service...";
        skeleton_.StopOfferService();
        std::cout << "and terminating, bye bye\n";
    }
};
} // namespace

Payload_transformation_plugin_interface::Uptr create_plugin(socom::Runtime& runtime)
{
    const auto instance_specifier_result =
        score::mw::com::InstanceSpecifier::Create(std::string{"xpad/cp60/MapApiLanesStamped"});
    if (!instance_specifier_result.has_value()) {
        std::cerr << "Invalid instance specifier: " << instance_specifier_result.error()
                  << std::endl;
        std::exit(EXIT_FAILURE);
    }

    auto create_result = IpcBridgeSkeleton::Create(instance_specifier_result.value());
    if (!create_result.has_value()) {
        std::cerr << "Unable to construct skeleton: " << create_result.error() << std::endl;
        std::exit(EXIT_FAILURE);
    }
    auto& skeleton = create_result.value();

    const auto offer_result = skeleton.OfferService();
    if (!offer_result.has_value()) {
        std::cerr << "Unable to offer service for skeleton: " << offer_result.error() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    return std::unique_ptr<Payload_transformation_plugin_interface,
                           Payload_transformation_plugin_deleter>(
        new Ipc_bridge_payload_transformator(std::move(skeleton), runtime), delete_plugin);
}

namespace {
Plugin_handle<Payload_transformation_plugin_factory> const handle(create_plugin);
}

static_assert(std::is_same<Payload_transformation_plugin_factory, decltype(create_plugin)*>::value);

} // namespace score::gateway
