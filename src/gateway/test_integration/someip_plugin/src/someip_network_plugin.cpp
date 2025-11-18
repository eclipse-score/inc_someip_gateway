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

#include <score/gateway/plugin_handle.hpp>
#include <score/gateway/service_configuration.hpp>
#include <score/gateway/someip_plugin_interface.hpp>
#include <score/socom/runtime.hpp>
#include <score/socom/server_connector.hpp>
#include <score/socom/vector_payload.hpp>

#include <cassert>
#include <string_view>
#include <type_traits>
#include <vector>

using score::gateway::get_instance;
using score::gateway::get_interface_configuration;
using score::gateway::Plugin_deleter;
using score::gateway::Someip_network_plugin_factory;
using score::gateway::Someip_network_plugin_interface;
using score::socom::Disabled_server_connector;
using score::socom::Enabled_server_connector;
using score::socom::Event_update_callback;
using score::socom::make_vector_payload;
using score::socom::Payload;
using score::socom::Runtime;
using score::socom::Service_instance;
using score::socom::Vector_buffer;

namespace score::someip_plugin {

void delete_plugin(Someip_network_plugin_interface* plugin)
{
    delete plugin;
}

// Creates a Vector_buffer from a list of unsigned integral type elements for testing purposes.
template<typename... Ts>
Payload::Sptr create_vector_payload(Ts... args) noexcept
{
    static_assert((std::is_same_v<char, Ts> && ...), "All arguments must be of type char.");
    Vector_buffer buffer{static_cast<Payload::Byte>(args)..., std::byte{0}};
    return make_vector_payload(std::move(buffer));
}

// TODO fix warning, might need to just return pointer or switch to C interface, or just avoid
//      dlopen() and link it statically
extern "C" std::unique_ptr<Someip_network_plugin_interface, Plugin_deleter>
create_plugin(Runtime& runtime, std::string_view const& /* network_interface */,
              std::string_view const& /* ip_address */,
              std::vector<std::string> const& /* manifests */)
{
    class Plugin_impl : public Someip_network_plugin_interface {
        Runtime& runtime_;
        Enabled_server_connector::Uptr server_connector_;

        Payload::Sptr const hello_gateway_payload_ =
            create_vector_payload('H', 'e', 'l', 'l', 'o', ' ', 'G', 'a', 't', 'e', 'w', 'a', 'y');

    public:
        Plugin_impl(Runtime& runtime) : runtime_(runtime)
        {
            assert(hello_gateway_payload_);
            assert(hello_gateway_payload_->data().size() == (5 + 1 + 7 + 1));

            Disabled_server_connector::Callbacks callbacks{
                [](auto&, auto, auto, auto const&, auto const&) { return nullptr; },
                [](auto&, auto, auto) {}, [](auto&, auto) {}};

            auto server_connector_result = runtime_.make_server_connector(
                get_interface_configuration(), get_instance(), std::move(callbacks));
            assert(server_connector_result);
            server_connector_ =
                Disabled_server_connector::enable(std::move(server_connector_result).value());
        }

        Runtime& get_runtime() { return runtime_; }

        void poll() override { server_connector_->update_event(0, hello_gateway_payload_); }
    };
    return std::unique_ptr<Someip_network_plugin_interface, Plugin_deleter>(
        new Plugin_impl(runtime), delete_plugin);
}

namespace {
gateway::Plugin_handle<Someip_network_plugin_factory> const handle(create_plugin);
}

static_assert(std::is_same<Someip_network_plugin_factory, decltype(create_plugin)*>::value);

} // namespace score::someip_plugin
