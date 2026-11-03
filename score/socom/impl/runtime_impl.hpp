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

#ifndef SCORE_SOCOM_RUNTIME_IMPL_HPP
#define SCORE_SOCOM_RUNTIME_IMPL_HPP

#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "client_connector_impl.hpp"
#include "runtime_registration.hpp"
#include "score/socom/runtime.hpp"
#include "score/socom/service_interface_definition.hpp"
#include "server_connector_impl.hpp"
#include "service_identifier.hpp"

namespace score::socom {

using CC_impl = client_connector::Impl;
using SC_impl = server_connector::Impl;

template <typename T>
struct Mutexed_variable {
    std::mutex mutex;
    T data{};
};

using Service_identifiers = Mutexed_variable<std::set<Service_instance_identifier>>;

class Service_record {
   public:
    struct Interfaced_server {
        Service_interface_identifier interface;
        SC_impl::Listen_endpoint endpoint;
    };

    struct Interfaced_client {
        Service_interface_identifier interface;
        CC_impl::Server_indication indication;
    };

    using Server = std::optional<Interfaced_server>;
    using Client = std::optional<Interfaced_client>;

    struct Server_registration {
        Registration registration;
        Client current_client;
    };

    struct Client_registration {
        Registration registration;
        Server current_server;
    };

    explicit Service_record(std::mutex& runtime_mutex);

    Server_registration register_server_connector(Service_interface_identifier const& interface,
                                                  SC_impl::Listen_endpoint connector);

    Result<Client_registration> register_client_connector(
        Service_interface_identifier const& interface, CC_impl::Server_indication on_server_update);

    bool is_available() const { return m_server.has_value(); }

   private:
    std::mutex& m_runtime_mutex;
    Server m_server;
    Client m_client;
};

using Instances = std::vector<Service_instance>;
using Interfaces_instances = std::unordered_map<Service_interface_identifier, Instances>;

class Service_database {
   public:
    explicit Service_database(std::mutex& runtime_mutex);

    Service_record& get_record(Service_interface_identifier const& interface,
                               Service_instance const& instance);

    Interfaces_instances get_instances(Service_interface_identifier const& interface,
                                       std::optional<Service_instance> const& filter) const;

    Interfaces_instances get_instances(std::optional<Service_interface_identifier> const& interface,
                                       std::optional<Service_instance> const& filter) const;

   private:
    struct Minor_version_ignoring_key_equal {
        bool operator()(Service_interface_identifier const& lhs,
                        Service_interface_identifier const& rhs) const noexcept {
            return (lhs.id == rhs.id) && (lhs.version.major == rhs.version.major);
        }
    };

    struct Minor_version_ignoring_hash {
        std::size_t operator()(Service_interface_identifier const& sii) const noexcept {
            auto const id_hash = std::hash<Registry_string_view>{}(sii.id);
            auto const major_hash = std::hash<std::uint16_t>{}(sii.version.major);
            return id_hash ^ (major_hash << 1);
        }
    };

    using Service_instances = std::unordered_map<Service_instance, Service_record>;
    using Service_interfaces =
        std::unordered_map<Service_interface_identifier, Service_instances,
                           Minor_version_ignoring_hash, Minor_version_ignoring_key_equal>;

    std::mutex& m_runtime_mutex;
    Service_interfaces m_service_records;
};

// both base classes delete the operator= in question
class Runtime_impl final : public Runtime {
   public:
    Result<Client_connector::Uptr> make_client_connector(
        Service_interface_definition configuration, Service_instance instance,
        Client_connector::Callbacks callbacks) noexcept override;

    Result<Client_connector::Uptr> make_client_connector(
        Service_interface_definition configuration, Service_instance instance,
        Client_connector::Callbacks callbacks,
        Posix_credentials const& credentials) noexcept override;

    Result<Disabled_server_connector::Uptr> make_server_connector(
        Server_service_interface_definition configuration, Service_instance instance,
        Disabled_server_connector::Callbacks callbacks) noexcept override;

    Result<Disabled_server_connector::Uptr> make_server_connector(
        Server_service_interface_definition configuration, Service_instance instance,
        Disabled_server_connector::Callbacks callbacks,
        Posix_credentials const& credentials) noexcept override;

    Result<Registration> register_connector(Service_interface_definition const& configuration,
                                            Service_instance const& instance,
                                            CC_impl::Server_indication const& on_server_update);

    Registration register_connector(Service_interface_identifier const& interface,
                                    Service_instance const& instance,
                                    SC_impl::Listen_endpoint endpoint);

   private:
    mutable std::mutex m_runtime_mutex{};
    Service_database m_database{m_runtime_mutex};

    std::shared_ptr<Service_identifiers> m_service_identifiers{
        std::make_shared<Service_identifiers>()};
};

}  // namespace score::socom

#endif  // SCORE_SOCOM_RUNTIME_IMPL_HPP
