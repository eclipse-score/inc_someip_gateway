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

#include <score/gateway/payload_transformation_plugin_interface.hpp>
#include <score/gateway/plugin_handle.hpp>
#include <score/gateway/someip_plugin_interface.hpp>

#include <utility>

namespace score::gateway {

template<typename PLUGIN_FACTORY>
std::map<void const*, PLUGIN_FACTORY>& Plugin_handle<PLUGIN_FACTORY>::plugin_functions()
{
    static std::map<void const*, PLUGIN_FACTORY> plugin_registry;
    return plugin_registry;
}

template<typename PLUGIN_FACTORY>
Plugin_handle<PLUGIN_FACTORY>::Plugin_handle(PLUGIN_FACTORY main_function)
{
    plugin_functions()[this] = std::move(main_function);
}

template<typename PLUGIN_FACTORY>
Plugin_handle<PLUGIN_FACTORY>::~Plugin_handle()
{
    plugin_functions().erase(this);
}

template<typename PLUGIN_FACTORY>
std::size_t Plugin_handle<PLUGIN_FACTORY>::get_num_plugins()
{
    return plugin_functions().size();
}

template<typename PLUGIN_FACTORY>
std::map<void const*, PLUGIN_FACTORY> const& Plugin_handle<PLUGIN_FACTORY>::get_plugin_functions()
{
    return plugin_functions();
}

template class Plugin_handle<Payload_transformation_plugin_factory>;
template class Plugin_handle<Someip_network_plugin_factory>;

} // namespace score::gateway
