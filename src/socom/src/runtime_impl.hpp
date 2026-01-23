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

#ifndef SCORE_SOCOM_RUNTIME_IMPL_HPP
#define SCORE_SOCOM_RUNTIME_IMPL_HPP

#include <atomic>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

#include "client_connector_impl.hpp"
#include "runtime_registration.hpp"
#include "score/socom/runtime.hpp"
#include "score/socom/service_interface_configuration.hpp"
#include "server_connector_impl.hpp"
#include "service_identifier.hpp"

namespace score {
namespace socom {

using CC_impl = client_connector::Impl;
using SC_impl = server_connector::Impl;

using Find_subscription_id = Find_subscription_handle const*;
using Bridge_registration_id = Service_bridge_registration_handle const*;

template <typename VALUE>
using Bridge_id_to = std::map<Bridge_registration_id, VALUE>;

using Bridge_id_to_subscription = Bridge_id_to<Find_subscription>;
using Bridge_id_to_request = Bridge_id_to<Service_request>;

template <typename INSTANCE, typename HANDLE>
using Active_bridge_requests = std::map<
    std::tuple<Service_interface_configuration, INSTANCE>,
    std::tuple<std::weak_ptr<Bridge_id_to<HANDLE>>, std::vector<std::optional<Bridge_identity>>>>;

template <typename T>
struct Mutexed_variable {
    std::mutex mutex;
    T data{};
};

using Currently_running_subscribe_find_service_report =
    Mutexed_variable<std::atomic<std::thread::id>>;

using Service_identifiers = Mutexed_variable<std::set<Service_identifier>>;

class Service_record {
   public:
    struct Interfaced_server {
        Service_interface interface;
        SC_impl::Listen_endpoint endpoint;
    };

    struct Interfaced_client {
        Service_interface interface;
        CC_impl::Server_indication indication;
    };

    using Server = std::optional<Interfaced_server>;
    using Clients = std::list<Interfaced_client>;

    struct Server_registration {
        Registration registration;
        Clients current_clients;
    };

    struct Client_registration {
        Registration registration;
        Server current_server;
    };

    explicit Service_record(std::mutex& runtime_mutex);

    Server_registration register_server_connector(Service_interface const& interface,
                                                  SC_impl::Listen_endpoint connector);

    Client_registration register_client_connector(Service_interface const& interface,
                                                  CC_impl::Server_indication on_server_update);

    bool is_available() const { return m_server.has_value(); }

   private:
    std::mutex& m_runtime_mutex;
    Server m_server;
    Clients m_clients;
};

using Instances = std::vector<Service_instance>;
using Interfaces_instances = std::map<Service_interface, Instances>;

class Service_database {
   public:
    explicit Service_database(std::mutex& runtime_mutex);

    Service_record& get_record(Service_interface const& interface,
                               Service_instance const& instance);

    Interfaces_instances get_instances(Service_interface const& interface,
                                       std::optional<Service_instance> const& filter) const;

    Interfaces_instances get_instances(std::optional<Service_interface> const& interface,
                                       std::optional<Service_instance> const& filter) const;

   private:
    struct Minor_version_ignoring_comparator {
        bool operator()(Service_interface const& lhs, Service_interface const& rhs) const;
    };

    using Service_instances = std::map<Service_instance, Service_record>;
    using Service_interfaces =
        std::map<Service_interface, Service_instances, Minor_version_ignoring_comparator>;

    std::mutex& m_runtime_mutex;
    Service_interfaces m_service_records;
};

// Could also be std::function instead.
// Which one is chosen is not a big deal as no user is exposed to it and can later be changed.
struct Stop_subscription {
    Stop_subscription() = default;
    Stop_subscription(Stop_subscription const&) = delete;
    Stop_subscription(Stop_subscription&&) = delete;
    virtual ~Stop_subscription() noexcept;
    Stop_subscription& operator=(Stop_subscription const&) = delete;
    Stop_subscription& operator=(Stop_subscription&&) = delete;

    virtual void stop_subscription(Find_subscription_id const& id) noexcept = 0;
    virtual void stop_registration(Bridge_registration_id const& id) noexcept = 0;
};

class Void_find_subscription_handle final : public Find_subscription_handle {};

class Find_subscription_handle_impl final : public Find_subscription_handle {
    Stop_subscription& m_stopper;

   public:
    explicit Find_subscription_handle_impl(Stop_subscription& stopper) : m_stopper{stopper} {}
    Find_subscription_handle_impl(Find_subscription_handle_impl const&) = delete;
    Find_subscription_handle_impl(Find_subscription_handle_impl&&) = delete;
    Find_subscription_handle_impl& operator=(Find_subscription_handle_impl const&) = delete;
    Find_subscription_handle_impl& operator=(Find_subscription_handle_impl&&) = delete;
    ~Find_subscription_handle_impl() noexcept override { m_stopper.stop_subscription(this); }
};

class Bridge_registration_handle_impl final : public Service_bridge_registration_handle {
   public:
    explicit Bridge_registration_handle_impl(Stop_subscription& stopper, Bridge_identity identity)
        : m_stopper{stopper}, m_identity{identity} {}
    Bridge_registration_handle_impl(Bridge_registration_handle_impl const&) = delete;
    Bridge_registration_handle_impl(Bridge_registration_handle_impl&&) = delete;
    Bridge_registration_handle_impl& operator=(Bridge_registration_handle_impl const&) = delete;
    Bridge_registration_handle_impl& operator=(Bridge_registration_handle_impl&&) = delete;
    ~Bridge_registration_handle_impl() noexcept override { m_stopper.stop_registration(this); };
    Bridge_identity get_identity() const override { return m_identity; }

   private:
    Stop_subscription& m_stopper;
    Bridge_identity m_identity;
};

// both base classes delete the operator= in question
class Runtime_impl final : public Runtime, public Stop_subscription {
   public:
    using Find_result_callback_wptr = std::weak_ptr<Find_result_change_callback const>;
    using Find_result_callback_sptr = std::shared_ptr<Find_result_change_callback const>;
    using Find_result_callbacks = std::list<Find_result_callback_wptr>;
    using Callback_with_id =
        std::tuple<Find_result_callback_sptr, std::optional<Service_interface>,
                   std::optional<Service_instance>, std::shared_ptr<Bridge_id_to_subscription>>;
    using Subscription_to_callback = std::map<Find_subscription_id, Callback_with_id>;
    using Service_instance_to_callbacks =
        std::map<std::optional<Service_instance>, Find_result_callbacks>;
    using Interface_to_instance_to_callbacks =
        std::map<std::optional<Service_interface>, Service_instance_to_callbacks>;

    using Bridge_registration_to_callbacks =
        Bridge_id_to<std::tuple<Subscribe_find_service_function, Request_service_function,
                                Interfaces_instances>>;

    Runtime::Result<Client_connector::Uptr> make_client_connector(
        Service_interface_configuration const& configuration, Service_instance const& instance,
        Client_connector::Callbacks callbacks) noexcept override;

    Runtime::Result<Client_connector::Uptr> make_client_connector(
        Service_interface_configuration const& configuration, Service_instance const& instance,
        Client_connector::Callbacks callbacks,
        Posix_credentials const& credentials) noexcept override;

    Runtime::Result<Disabled_server_connector::Uptr> make_server_connector(
        Server_service_interface_configuration const& configuration,
        Service_instance const& instance,
        Disabled_server_connector::Callbacks callbacks) noexcept override;

    Runtime::Result<Disabled_server_connector::Uptr> make_server_connector(
        Server_service_interface_configuration const& configuration,
        Service_instance const& instance, Disabled_server_connector::Callbacks callbacks,
        Posix_credentials const& credentials) noexcept override;

    Find_subscription subscribe_find_service(
        Find_result_callback on_result_set_change, Service_interface const& interface,
        std::optional<Service_instance> instance) noexcept override;

    Find_subscription subscribe_find_service(
        Find_result_change_callback on_result_change, std::optional<Service_interface> interface,
        std::optional<Service_instance> instance,
        std::optional<Bridge_identity> identity) noexcept override;

    // NOLINTBEGIN(bugprone-exception-escape)(ClangTidy Android Warning)
    Runtime::Result<Service_bridge_registration> register_service_bridge(
        Bridge_identity identity, Subscribe_find_service_function subscribe_find_service,
        Request_service_function request_service) noexcept override;
    // NOLINTEND(bugprone-exception-escape)

    Registration register_connector(Service_interface_configuration const& configuration,
                                    Service_instance const& instance,
                                    CC_impl::Server_indication const& on_server_update);

    Registration register_connector(Service_interface const& interface,
                                    Service_instance const& instance,
                                    SC_impl::Listen_endpoint endpoint);

    void stop_subscription(Find_subscription_id const& id) noexcept override;

    void stop_registration(Bridge_registration_id const& id) noexcept override;

   private:
    std::shared_ptr<Bridge_id_to_request> get_or_create_service_requests(
        Service_interface_configuration const& configuration, Service_instance const& instance);

    void remove_from_service_requests(Service_interface_configuration const& configuration,
                                      Service_instance const& instance);

    Registration bridge_service_requests(Service_interface_configuration const& configuration,
                                         Service_instance const& instance);

    void update_bridges_provided_services(Bridge_registration_id const& bridge_id,
                                          Service_interface const& interface,
                                          Service_instance const& instance,
                                          Find_result_status status);

    void call_subscribe_find_service_callbacks(Service_interface const& interface,
                                               Service_instance const& instance,
                                               Find_result_status status, bool local);

    Find_result_change_callback create_bridge_find_result_callback(
        Bridge_registration_id const& bridge_id);

    std::shared_ptr<Bridge_id_to_subscription> get_or_create_find_services(
        Service_interface const& interface, std::optional<Service_instance> const& instance,
        std::optional<Bridge_identity> identity);

    Interfaces_instances get_bridge_reported_instances(
        Service_interface const& interface, std::optional<Service_instance> const& instance) const;

    mutable std::mutex m_runtime_mutex{};
    Service_database m_database{m_runtime_mutex};

    Subscription_to_callback m_find_service_subscriptions{};
    Interface_to_instance_to_callbacks m_interface_to_callbacks{};

    mutable std::mutex m_bridge_mutex;
    Bridge_registration_to_callbacks m_bridge_to_callbacks{};

    Active_bridge_requests<std::optional<Service_instance>, Find_subscription>
        m_active_bridge_find_services{};

    Active_bridge_requests<Service_instance, Service_request> m_service_requests{};

    std::shared_ptr<Service_identifiers> m_service_identifiers{
        std::make_shared<Service_identifiers>()};

    // Serialize Find_result_callback calls to reliably detect callback self deletion
    Currently_running_subscribe_find_service_report m_currently_running_service_report;
};

}  // namespace socom
}  // namespace score

#endif  // SCORE_SOCOM_RUNTIME_IMPL_HPP
