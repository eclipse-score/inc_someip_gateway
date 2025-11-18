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

#ifndef SCORE_MW_COM_IPC_BRIDGE_PLUGIN_INTERFACE_H
#define SCORE_MW_COM_IPC_BRIDGE_PLUGIN_INTERFACE_H

#include <memory>
#include <string_view>
#include <vector>

namespace score::socom {
class Runtime;
}

namespace score::gateway {

class Someip_network_plugin_interface;
using Plugin_deleter = void (*)(Someip_network_plugin_interface*);

/// \brief Plugin interface for SOME/IP network plugins.
///
/// It bridges a network interface to socom and supports many service interfaces and instances.
///
// TODO it might make sense to have a pure C interface so that e.g. Rust can also implement the
// plugin interface, but this requires socom to have a C interface as well
class Someip_network_plugin_interface {
public:
    using Uptr = std::unique_ptr<Someip_network_plugin_interface, Plugin_deleter>;

    // default destructor needs to be in cpp file to work around ODR violations when dlopen()
    // plugins
    virtual ~Someip_network_plugin_interface();

    /// \brief Trigger internal processing
    ///
    /// Currently used by test to avoid threads. Interface is subject to change.
    virtual void poll() = 0;
};

/// \brief Creates an instance of a SOME/IP network plugin.
///
/// \param[in] runtime socom::Runtime which connects provided and required services
/// \param[in] network_interface network interface to bind SOME/IP communication to
/// \param[in] ip_address IP address to bind SOME/IP communication to
/// \param[in] manifests paths to SOME/IP manifest files with SOME/IP service and network configuration
/// \return pointer to created plugin instance
using Someip_network_plugin_factory = Someip_network_plugin_interface::Uptr (*)(
    ::score::socom::Runtime& runtime, std::string_view const& network_interface,
    std::string_view const& ip_address, std::vector<std::string> const& manifests);

} // namespace score::gateway

#endif // SCORE_MW_COM_IPC_BRIDGE_SAMPLE_SENDER_RECEIVER_H
