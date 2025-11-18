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

#ifndef SCORE_GATEWAY_PLUGIN_HANDLE_H
#define SCORE_GATEWAY_PLUGIN_HANDLE_H

#include <map>

namespace score::gateway {

/// \brief Payload_transformation_plugin_handle implements the RAII pattern for plugin registration.
/// It registers the plugin main function at construction and deregisters it at
/// destruction.
template<typename PLUGIN_FACTORY>
class Plugin_handle {
    // especially this function has to be defined in cpp file to have a single instance across
    // translation units
    static std::map<void const*, PLUGIN_FACTORY>& plugin_functions();

public:
    Plugin_handle(PLUGIN_FACTORY main_function);

    Plugin_handle(const Plugin_handle&) = delete;
    Plugin_handle(Plugin_handle&& other) noexcept = delete;

    ~Plugin_handle();

    Plugin_handle& operator=(const Plugin_handle&) = delete;
    Plugin_handle& operator=(Plugin_handle&& other) noexcept = delete;

    /// \brief Get the number of loaded plugins.
    static std::size_t get_num_plugins();

    /// \brief Get the plugin functions.
    /// The key actually does not make sense for users, but is kept for simplicity.
    /// The plugin function is behind the value.
    /// The key is used to uniquely identify each plugin's main function.
    static std::map<void const*, PLUGIN_FACTORY> const& get_plugin_functions();
};

} // namespace score::gateway

#endif // SCORE_GATEWAY_PLUGIN_HANDLE_H
