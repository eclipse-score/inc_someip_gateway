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

#include "runtime_impl.hpp"

#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <iterator>
#include <memory>
#include <tuple>

#include "client_connector_impl.hpp"
#include "final_action.hpp"
#include "score/socom/client_connector.hpp"
#include "score/socom/runtime.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/service_interface.hpp"
#include "server_connector_impl.hpp"

using score::cpp::make_unexpected;

namespace score {
namespace socom {

namespace {

bool is_matching_instance(std::optional<Service_instance> const& filter,
                          Service_instance const& instance) {
    return !filter || *filter == instance;
}

template <typename Key, typename Value>
std::vector<Key> get_keys(std::map<Key, Value> const& map) {
    std::vector<Key> keys;
    keys.reserve(map.size());
    for (auto const& pair : map) {
        keys.emplace_back(pair.first);
    }
    return keys;
}

std::optional<Service_interface> const& any_interface() {
    static auto const any = std::optional<Service_interface>{};
    return any;
}

std::optional<Service_instance> const& any_instance() {
    static auto const any = std::optional<Service_instance>{};
    return any;
}

void get_callbacks_to_notify_helper(
    std::vector<Runtime_impl::Find_result_callback_wptr>& result,
    Runtime_impl::Service_instance_to_callbacks const& instances_to_callbacks,
    std::optional<Service_instance> const& instance) {
    auto const filter_callbacks = instances_to_callbacks.find(instance);
    if (std::end(instances_to_callbacks) != filter_callbacks) {
        result.insert(std::end(result), std::begin(filter_callbacks->second),
                      std::end(filter_callbacks->second));
    }
}

std::vector<Runtime_impl::Find_result_callback_wptr> get_callbacks_to_notify(
    Runtime_impl::Interface_to_instance_to_callbacks const& interface_to_callbacks,
    Service_interface const& interface, Service_instance const& instance, bool const local) {
    std::vector<Runtime_impl::Find_result_callback_wptr> result;

    if (local) {
        auto const any_interface_instances_to_callbacks =
            interface_to_callbacks.find(any_interface());
        if (std::end(interface_to_callbacks) != any_interface_instances_to_callbacks) {
            get_callbacks_to_notify_helper(result, any_interface_instances_to_callbacks->second,
                                           any_instance());
        }
    }

    auto const instances_to_callbacks =
        interface_to_callbacks.find(std::optional<Service_interface>{interface});
    if (std::end(interface_to_callbacks) != instances_to_callbacks) {
        get_callbacks_to_notify_helper(result, instances_to_callbacks->second, any_instance());
        get_callbacks_to_notify_helper(result, instances_to_callbacks->second,
                                       std::optional<Service_instance>{instance});
    }

    return result;
}

std::thread::id get_invalid_thread_id() { return {}; }

void notify_subscribed_callbacks(
    Currently_running_subscribe_find_service_report& running_service_report,
    std::vector<Runtime_impl::Find_result_callback_wptr> const& callbacks_to_notify,
    Service_interface const& interface, Service_instance const& instance,
    Find_result_status const status) {
    auto const call_callbacks_to_notify = [&callbacks_to_notify, &interface, &instance, &status]() {
        for (auto const& cb : callbacks_to_notify) {
            auto const locked_cb = cb.lock();
            if (nullptr != locked_cb) {
                (*locked_cb)(interface, instance, status);
            }
        }
    };

    // check if thread id was already set and matches this_thread::id, if yes Server_connector
    // creation was triggered by callback and skip locking
    if (std::this_thread::get_id() == running_service_report.data) {
        call_callbacks_to_notify();
    } else {
        // Serialize Find_result_callback calls to reliably detect callback self deletion
        // save and remove current thread id, which is checked in stop_subscription()
        std::lock_guard<std::mutex> const lock{running_service_report.mutex};
        running_service_report.data = std::this_thread::get_id();
        Final_action const reset_data{
            [&running_service_report]() { running_service_report.data = get_invalid_thread_id(); }};

        call_callbacks_to_notify();
    }
}

template <typename T>
void cleanup(std::list<std::weak_ptr<T const>>& list,
             std::shared_ptr<T const> const& item) noexcept {
    auto const equals_cb = [&item](auto const& cb_ref) {
        auto const locked_cb = cb_ref.lock();

        // Defensive programming. The true case for the condition (nullptr == locked_cb) will not
        // occur because Runtime_impl implements Stop_subscription which further calls
        // stop_subscribe when Find_subscription_handle_impl gets destroyed. This will trigger the
        // cleanup function and the Find_result_callback gets removed from the list.
        // Find_result_callback can not be destroyed from outside in a test to trigger the true
        // case for (nullptr == locked_cb).

        return (nullptr == locked_cb) || (item == locked_cb);
    };

    list.remove_if(equals_cb);
}

template <typename Key, typename Value>
void cleanup(std::map<Key, Value>& map, Key const& key, Value const& value) noexcept {
    if (value.empty()) {
        map.erase(key);
    }
}

template <typename Key0, typename Key1, typename Value>
void cleanup(std::map<Key0, std::tuple<std::weak_ptr<std::map<Key1, Value>>,
                                       std::vector<std::optional<Bridge_identity>>>>& map,
             Key1 const& key) noexcept {
    for (auto const& values : map) {
        auto const value_locked = std::get<0>(values.second).lock();

        if (nullptr != value_locked) {
            value_locked->erase(key);
        }
        // Cannot be covered, there is no reliable way to delete values.second other than using
        // stop_registration which calls this cleanup-function.
        // Sporadically covered by
        // TEST_F(RuntimeMultiThreadingTest,
        //     BridgesAndSubscribeFindServiceHaveNoRaceConditions)
        else {
        }
    }
}

template <typename Key, typename Value>
void cleanup(
    std::map<Key, std::tuple<std::weak_ptr<Value>, std::vector<std::optional<Bridge_identity>>>>&
        map,
    Key const& key) noexcept {
    auto const request = map.find(key);
    // Cannot be covered, there is no reliable way to delete key from map other than using
    // stop_subscription which calls this cleanup-function.
    // Sporadically covered by
    // TEST_F(RuntimeMultiThreadingTest,
    // CreationOfMultipleServerAndClientConnectorsHasNoRaceCondition)

    if (std::end(map) == request) {
        return;
    }

    auto const request_locked = std::get<0>(request->second).lock();
    if (nullptr == request_locked) {
        map.erase(request);
    }
}

class Final_action_registration final : public IRegistration {
    // will be executed as a final action on destruction.
    Final_action m_final_action;

   public:
    explicit Final_action_registration(Final_action final_action)
        : m_final_action{std::move(final_action)} {}
};

class Registration_collection final : public IRegistration {
   public:
    Registration_collection(Registration r1, Registration r2)
        : m_registration0{std::move(r1)}, m_registration1{std::move(r2)} {}

   private:
    Registration m_registration0;
    Registration m_registration1;
};

bool is_minor_version_compatible(Service_interface const& server, Service_interface const& client) {
    return client.version.minor <= server.version.minor;
}

bool is_interface_compatible(Service_interface const& server, Service_interface const& client) {
    // Defensive programming. This function is called in the two register_connector functions.
    // First, a record is loaded depending on service interface and instance. The record ensures
    // that the right server/client is loaded, therefore this function will always return true.

    return (server.id == client.id) && (server.version.major == client.version.major) &&
           is_minor_version_compatible(server, client);
}

bool is_only_minor_version_incompatible(Service_interface const& server,
                                        Service_interface const& client) {
    // Defensive programming. This function is called in the two register_connector functions.
    // First, a record is loaded depending on service interface and instance. The record ensures
    // that the right server/client is loaded, therefore this function will always return true.

    return (server.id == client.id) && (server.version.major == client.version.major) &&
           (client.version.minor > server.version.minor);
}

bool is_valid(Client_connector::Callbacks const& callbacks) {
    return callbacks.on_service_state_change && callbacks.on_event_update &&
           callbacks.on_event_requested_update && callbacks.on_event_subscription_status_change;
}

bool is_valid(Disabled_server_connector::Callbacks const& callbacks) {
    return callbacks.on_method_call && callbacks.on_event_update_request &&
           callbacks.on_event_subscription_change;
}

// actually understandable and easily reviewed, a code change is not justified.
template <typename Instance, typename Callback, typename CreateValue>
void register_bridge(Bridge_registration_id const& bridge_id,
                     std::unique_lock<std::mutex>& bridge_lock,
                     Active_bridge_requests<Instance, Callback> const& bridge_requests,
                     CreateValue const& create_value) {
    using Abr = Active_bridge_requests<Instance, Callback>;
    using Key_t = typename Abr::key_type;
    using Value_t =
        typename std::tuple_element<0, typename Abr::mapped_type>::type::element_type::mapped_type;
    assert(bridge_lock.owns_lock());

    /// THE algorithm:
    // copy bridge_requests
    std::set<Key_t> requests_done;
    auto requests_to_do = get_keys(bridge_requests);
    bool first_run = true;
    // would have been do {} while(); but Axivion forbids it
    while (!requests_to_do.empty() || first_run) {
        first_run = false;
        // unlock
        bridge_lock.unlock();

        // call callbacks and store result in copy
        std::map<Key_t, Value_t> request_with_callback_result;
        for (auto const& request : requests_to_do) {
            request_with_callback_result[request] = create_value(request);
            requests_done.insert(request);
        }
        requests_to_do.clear();

        // lock
        bridge_lock.lock();

        // merge and compute difference
        for (auto const& request : bridge_requests) {
            auto const cb_result_iter = request_with_callback_result.find(request.first);
            if (std::end(request_with_callback_result) == cb_result_iter) {
                // ensure each request is only processed once
                if (std::end(requests_done) == requests_done.find(request.first)) {
                    // new request at Runtime-API was done. Need to add it to new bridge as well
                    requests_to_do.emplace_back(request.first);
                }
            } else {
                auto const bridge_to_request = std::get<0>(request.second).lock();
                // if destruction of Client_connector or subscribe_find_service handle is
                // concurrently happening there might be a nullptr in the map until the map is
                // cleaned. Thus because the map is going to be cleaned do not bother to call the
                // callback of the bridge

                if (nullptr != bridge_to_request) {
                    bridge_to_request->emplace(bridge_id, std::move(cb_result_iter->second));
                }
                // Cannot be covered, there is no reliable way to delete request.second during the
                // unlocked create_value-loop.
                // Sporadically covered by
                // TEST_F(RuntimeMultiThreadingTest,
                //     BridgesAndSubscribeFindServiceHaveNoRaceConditions)
                else {
                }
            }
        }
        // repeat with new requests
    }

    // leave function locked
    assert(bridge_lock.owns_lock());
}

bool is_forward_subscription(
    std::optional<Bridge_identity> const& identity, Bridge_identity const& bridge_callback_identity,
    std::vector<std::optional<Bridge_identity>> const& subscriber_identity_record) {
    bool const is_first_subscription = subscriber_identity_record.size() == 1U;
    bool const is_second_subscription = subscriber_identity_record.size() == 2U;
    bool const is_current_subscriber = identity && (*identity == bridge_callback_identity);

    bool forward_subscription = false;
    if (is_first_subscription) {
        forward_subscription = !is_current_subscriber;
    } else if (is_second_subscription) {
        bool const is_already_subscribed =
            (std::find(subscriber_identity_record.begin(), subscriber_identity_record.end(),
                       bridge_callback_identity) != subscriber_identity_record.end());

        forward_subscription = is_already_subscribed && !is_current_subscriber;
    } else {
        // Nothing to do: satisfies AutosarC++18_10-M6.4.2
    }
    return forward_subscription;
}

template <typename ReturnValue, typename Instance, typename Instance1, typename Handle,
          typename CreateValue>
std::shared_ptr<ReturnValue> get_bridge_requests(
    Service_interface_configuration const& configuration, Instance const& instance,
    std::optional<Bridge_identity> identity, std::unique_lock<std::mutex>& bridge_lock,
    Active_bridge_requests<Instance1, Handle>& active_requests,
    Runtime_impl::Bridge_registration_to_callbacks const& bridge_to_callback,
    CreateValue const& create_value) {
    assert(bridge_lock.owns_lock());
    auto const key = std::make_tuple(configuration, instance);
    auto const find_services = active_requests.find(key);
    std::shared_ptr<ReturnValue> result = (std::end(active_requests) == find_services)
                                              ? nullptr
                                              : std::get<0>(find_services->second).lock();

    if (nullptr == result) {
        result = std::make_shared<ReturnValue>();
        active_requests[key] =
            std::make_tuple(result, std::vector<std::optional<Bridge_identity>>{});
    }

    auto& subscriber_identity_record = std::get<1>(active_requests[key]);

    auto const bridge_to_callback_copy = bridge_to_callback;
    ReturnValue tmp_result;

    subscriber_identity_record.emplace_back(identity);

    for (auto const& bridge : bridge_to_callback_copy) {
        bool const forward_subscription = is_forward_subscription(
            identity, bridge.first->get_identity(), subscriber_identity_record);

        if (forward_subscription) {
            bridge_lock.unlock();
            tmp_result.emplace(bridge.first, create_value(bridge, configuration, instance));
            bridge_lock.lock();
        }
    }

    for (auto& bridge_id_callback_result : tmp_result) {
        // The false condition case might occur between bridge_lock.unlock() and bridge_lock.lock()
        // when a subscription is beeing forwarded. It could be observed that the false case occurs
        // sporadically with the following test:
        // TEST_F(RuntimeMultiThreadingTest, BridgesAndSubscribeFindServiceHaveNoRaceConditions)
        //
        // This leads to the assumption that we are dealing with a race condition here which does
        // not lead to any problems/errors.
        // TODO: This case should be further discussed.

        if (std::end(*result) == result->find(bridge_id_callback_result.first)) {
            result->emplace(bridge_id_callback_result.first,
                            std::move(bridge_id_callback_result.second));
        }
    }
    return result;
}

void call_find_result_callback_with_currently_known_services(
    Find_result_change_callback const& on_result_set_change, Interfaces_instances const& services) {
    for (auto const& interface_with_instances : services) {
        for (auto const& instance : interface_with_instances.second) {
            on_result_set_change(interface_with_instances.first, instance,
                                 Find_result_status::added);
        }
    }
}

}  // namespace

Service_database::Service_database(std::mutex& runtime_mutex) : m_runtime_mutex{runtime_mutex} {}

Service_record& Service_database::get_record(Service_interface const& interface,
                                             Service_instance const& instance) {
    auto it_interface = m_service_records.find(interface);
    if (it_interface == std::end(m_service_records)) {
        it_interface = m_service_records.emplace(interface, Service_instances{}).first;
    }

    auto& record =
        it_interface->second.emplace(instance, Service_record{m_runtime_mutex}).first->second;

    return record;
}

Interfaces_instances Service_database::get_instances(
    Service_interface const& interface, std::optional<Service_instance> const& filter) const {
    auto const it_interface = m_service_records.find(interface);
    if (std::end(m_service_records) == it_interface) {
        return {};
    }
    auto const& instances = it_interface->second;
    auto const matches_filter = [&filter](Service_instances::value_type const& instance) {
        return (instance.second.is_available() && is_matching_instance(filter, instance.first));
    };

    Service_instances filtered_instances;
    (void)std::copy_if(std::begin(instances), std::end(instances),
                       std::inserter(filtered_instances, std::end(filtered_instances)),
                       matches_filter);
    return {{interface, get_keys(filtered_instances)}};
}

Interfaces_instances Service_database::get_instances(
    std::optional<Service_interface> const& interface,
    std::optional<Service_instance> const& filter) const {
    if (interface) {
        return get_instances(*interface, filter);
    }

    Interfaces_instances result{};
    for (auto const& interface_with_instances : m_service_records) {
        auto& instances = result[interface_with_instances.first];
        for (auto const& instance : interface_with_instances.second) {
            instances.emplace_back(instance.first);
        }
    }
    return result;
}

bool Service_database::Minor_version_ignoring_comparator::operator()(
    Service_interface const& lhs, Service_interface const& rhs) const {
    return std::tie(lhs.id, lhs.version.major) < std::tie(rhs.id, rhs.version.major);
}

Stop_subscription::~Stop_subscription() noexcept = default;

Service_record::Service_record(std::mutex& runtime_mutex) : m_runtime_mutex{runtime_mutex} {}

Service_record::Server_registration Service_record::register_server_connector(
    Service_interface const& interface, SC_impl::Listen_endpoint connector) {
    // Duplicate server connectors are not allowed.
    assert(!m_server);

    m_server.emplace(Interfaced_server{interface, std::move(connector)});
    auto final_action = [this]() {
        {
            std::lock_guard<std::mutex> const lock{m_runtime_mutex};
            m_server.reset();
        }
    };

    return Server_registration{
        std::make_unique<Final_action_registration>(Final_action(std::move(final_action))),
        m_clients};
}

Service_record::Client_registration Service_record::register_client_connector(
    Service_interface const& interface, CC_impl::Server_indication on_server_update) {
    auto const it = m_clients.emplace(m_clients.end(),
                                      Interfaced_client{interface, std::move(on_server_update)});

    auto remove_from_registry = [this, it]() {
        std::lock_guard<std::mutex> const lock{m_runtime_mutex};
        m_clients.erase(it);
    };

    return Client_registration{
        std::make_unique<Final_action_registration>(Final_action(std::move(remove_from_registry))),
        m_server};
}

Result<Client_connector::Uptr> Runtime_impl::make_client_connector(
    Service_interface_configuration const& configuration, Service_instance const& instance,
    Client_connector::Callbacks callbacks) noexcept {
    return make_client_connector(configuration, instance, std::move(callbacks),
                                 Posix_credentials{::getuid(), ::getgid()});
}

Result<Client_connector::Uptr> Runtime_impl::make_client_connector(
    Service_interface_configuration const& configuration, Service_instance const& instance,
    Client_connector::Callbacks callbacks, Posix_credentials const& credentials) noexcept {
    if (!is_valid(callbacks)) {
        return MakeUnexpected(Construction_error::callback_missing);
    }

    return {std::make_unique<CC_impl>(*this, configuration, instance, std::move(callbacks),
                                      credentials)};
}

Result<Disabled_server_connector::Uptr> Runtime_impl::make_server_connector(
    Server_service_interface_configuration const& configuration, Service_instance const& instance,
    Disabled_server_connector::Callbacks callbacks) noexcept {
    return make_server_connector(configuration, instance, std::move(callbacks),
                                 Posix_credentials{::getuid(), ::getgid()});
}

Result<Disabled_server_connector::Uptr> Runtime_impl::make_server_connector(
    Server_service_interface_configuration const& configuration, Service_instance const& instance,
    Disabled_server_connector::Callbacks callbacks, Posix_credentials const& credentials) noexcept {
    Service_identifier const identifier{configuration.get_interface(), instance};

    if (!is_valid(callbacks)) {
        return MakeUnexpected(Construction_error::callback_missing);
    }

    {
        std::lock_guard<std::mutex> const lock(this->m_service_identifiers->mutex);
        if (!m_service_identifiers->data.insert(identifier).second) {
            return MakeUnexpected(Construction_error::duplicate_service);
        }
    }

    Final_action final_action{
        [identifier, identifiers = std::weak_ptr<Service_identifiers>{m_service_identifiers}]() {
            auto const locked_identifiers = identifiers.lock();
            if (locked_identifiers) {
                std::lock_guard<std::mutex> const lock(locked_identifiers->mutex);
                locked_identifiers->data.erase(identifier);
            }
        }};

    return {std::make_unique<SC_impl>(*this, configuration, instance, std::move(callbacks),
                                      std::move(final_action), credentials)};
}

namespace {

class Find_aggregation {
   public:
    explicit Find_aggregation(Find_result_callback cb);

    void initial_indicate() const;
    void on_result_set_change(Service_instance const& instance, Find_result_status status);

   private:
    void add(Service_instance const& instance);
    void remove(Service_instance const& instance);
    void indicate() const;

    mutable std::mutex m_mutex{};
    mutable bool m_collecting_initial_result{true};
    Find_result_container m_results{};
    Find_result_callback const m_on_result_set_change;
};

Find_aggregation::Find_aggregation(Find_result_callback cb)
    : m_on_result_set_change{std::move(cb)} {}

void Find_aggregation::initial_indicate() const {
    std::lock_guard<std::mutex> const guard{m_mutex};
    m_on_result_set_change(m_results);
    m_collecting_initial_result = false;
}

void Find_aggregation::on_result_set_change(Service_instance const& instance,
                                            Find_result_status status) {
    if (status == Find_result_status::added) {
        add(instance);
    } else {
        assert(status == Find_result_status::deleted);
        remove(instance);
    }
}

void Find_aggregation::add(Service_instance const& instance) {
    std::lock_guard<std::mutex> const guard{m_mutex};
    m_results.push_back(instance);
    indicate();
}

void Find_aggregation::remove(Service_instance const& instance) {
    std::lock_guard<std::mutex> const guard{m_mutex};
    auto const it_end = m_results.end();
    auto const it = std::find(m_results.begin(), it_end, instance);
    if (it != it_end) {
        m_results.erase(it);
        indicate();
    }
}

void Find_aggregation::indicate() const {
    if (!m_collecting_initial_result) {
        m_on_result_set_change(m_results);
    }
}

class Find_aggregation_subscription_handle : public Find_subscription_handle {
   public:
    Find_aggregation_subscription_handle(Find_subscription fs, std::shared_ptr<Find_aggregation> fa)
        : m_find_aggregation{std::move(fa)}, m_handle{std::move(fs)} {
        m_find_aggregation->initial_indicate();
    }

    Find_aggregation_subscription_handle(Find_aggregation_subscription_handle const& other) =
        delete;
    Find_aggregation_subscription_handle(Find_aggregation_subscription_handle&& other) = delete;

    ~Find_aggregation_subscription_handle() override = default;

    Find_aggregation_subscription_handle& operator=(
        Find_aggregation_subscription_handle const& other) = delete;
    Find_aggregation_subscription_handle& operator=(Find_aggregation_subscription_handle&& other) =
        delete;

   private:
    std::shared_ptr<Find_aggregation> m_find_aggregation;
    Find_subscription m_handle;
};

}  // namespace

Find_subscription Runtime_impl::subscribe_find_service(
    Find_result_callback on_result_set_change, Service_interface const& interface,
    std::optional<Service_instance> instance) noexcept {
    // shared context between multiple copies of the callback handler
    auto find_aggregation = std::make_shared<Find_aggregation>(std::move(on_result_set_change));

    auto update_find_result_container_handler =
        [find_aggregation](auto const& /*interface*/, auto const& instance, auto status) mutable {
            find_aggregation->on_result_set_change(instance, status);
        };

    // Wrap the subscription handle in order to tie the scope of shared context with that of the
    // subscription handle.
    Find_subscription find_subscription{std::make_unique<Find_aggregation_subscription_handle>(
        subscribe_find_service(std::move(update_find_result_container_handler), interface, instance,
                               std::optional<Bridge_identity>{}),
        find_aggregation)};

    return find_subscription;
}

Find_subscription Runtime_impl::subscribe_find_service(
    Find_result_change_callback on_result_change, std::optional<Service_interface> interface,
    std::optional<Service_instance> instance, std::optional<Bridge_identity> identity) noexcept {
    assert(interface || !instance);

    if (!on_result_change) {
        return std::make_unique<Void_find_subscription_handle>();
    }

    // call callback with current active services
    std::unique_lock<std::mutex> lock{m_runtime_mutex};

    auto find_handle = std::make_unique<Find_subscription_handle_impl>(*this);
    auto const inserted_element = m_find_service_subscriptions.emplace(
        find_handle.get(),
        std::make_tuple(std::make_shared<Find_result_change_callback>(on_result_change), interface,
                        instance, nullptr));
    assert(inserted_element.second);
    m_interface_to_callbacks[interface][instance].emplace_back(
        std::get<0>(inserted_element.first->second));

    auto const current_interfaces_instances = m_database.get_instances(interface, instance);
    lock.unlock();

    try {
        call_find_result_callback_with_currently_known_services(on_result_change,
                                                                current_interfaces_instances);

        if (interface) {
            auto const bridge_instances = get_bridge_reported_instances(*interface, instance);
            call_find_result_callback_with_currently_known_services(on_result_change,
                                                                    bridge_instances);
        }
    } catch (...) {
    }

    // wildcard interface searches shall not be forwarded to bridges
    if (interface) {
        auto bridge_find_subscriptions =
            get_or_create_find_services(*interface, instance, identity);
        lock.lock();
        std::get<3>(inserted_element.first->second) = std::move(bridge_find_subscriptions);
    }

    return find_handle;
}

Result<Service_bridge_registration> Runtime_impl::register_service_bridge(
    Bridge_identity identity, Subscribe_find_service_function subscribe_find_service,
    Request_service_function request_service) noexcept {
    if (!subscribe_find_service || !request_service) {
        return MakeUnexpected(Construction_error::callback_missing);
    }
    // false positive, registration is moved at return
    // stack allocation not possible as the object needs a stable memory address
    auto registration = std::make_unique<Bridge_registration_handle_impl>(*this, identity);

    auto const create_find_subscription =
        [this, subscribe_find_service,
         bridge_id = registration.get()](auto const& interface_configuration) {
            auto const& interface = std::get<0>(interface_configuration).interface;
            auto const& instance = std::get<1>(interface_configuration);
            std::optional<Service_instance> const ref_instance =
                instance ? *instance : std::optional<Service_instance>{};
            Find_result_change_callback cb = this->create_bridge_find_result_callback(bridge_id);

            return subscribe_find_service(std::move(cb), interface, ref_instance);
        };

    auto const create_service_request = [request_service](auto const& interface_configuration) {
        return request_service(std::get<0>(interface_configuration),
                               std::get<1>(interface_configuration));
    };

    std::unique_lock<std::mutex> lock{m_bridge_mutex};
    m_bridge_to_callbacks[registration.get()] = std::make_tuple(
        std::move(subscribe_find_service), std::move(request_service), Interfaces_instances{});

    register_bridge(registration.get(), lock, m_active_bridge_find_services,
                    create_find_subscription);
    register_bridge(registration.get(), lock, m_service_requests, create_service_request);

    return Result<Service_bridge_registration>{std::move(registration)};
}

Registration Runtime_impl::register_connector(Service_interface_configuration const& configuration,
                                              Service_instance const& instance,
                                              CC_impl::Server_indication const& on_server_update) {
    std::unique_lock<std::mutex> lock{m_runtime_mutex};
    auto& sii_record = m_database.get_record(configuration.interface, instance);
    auto result = sii_record.register_client_connector(configuration.interface, on_server_update);
    lock.unlock();

    Registration bridged_service_requests;
    if (result.current_server) {
        if (is_minor_version_compatible(result.current_server->interface,
                                        configuration.interface)) {
            assert(
                is_interface_compatible(result.current_server->interface, configuration.interface));
            on_server_update(result.current_server->endpoint);
        } else {
            assert(is_only_minor_version_incompatible(result.current_server->interface,
                                                      configuration.interface));
            std::cerr << "SOCom error: Bind client to server - minor version incompatible:"
                      << " client=" << configuration.interface.id
                      << ", server=" << result.current_server->interface.id
                      << ", instance=" << instance << std::endl;
        }
    } else {
        bridged_service_requests = bridge_service_requests(configuration, instance);
    }

    return std::make_unique<Registration_collection>(std::move(bridged_service_requests),
                                                     std::move(result.registration));
}

Registration Runtime_impl::register_connector(Service_interface const& interface,
                                              Service_instance const& instance,
                                              SC_impl::Listen_endpoint endpoint) {
    std::unique_lock<std::mutex> lock{m_runtime_mutex};
    auto& sii_record = m_database.get_record(interface, instance);
    auto result = sii_record.register_server_connector(interface, endpoint);
    auto const callbacks_to_notify =
        get_callbacks_to_notify(m_interface_to_callbacks, interface, instance, true);
    lock.unlock();

    auto const connect_client = [&endpoint, &interface, &instance](auto const& client) {
        if (is_minor_version_compatible(interface, client.interface)) {
            assert(is_interface_compatible(interface, client.interface));
            client.indication(endpoint);
        } else {
            assert(is_only_minor_version_incompatible(interface, client.interface));
            std::cerr << "SOCom error: Bind client to server - minor version incompatible:"
                      << " client=" << client.interface.id << ", server=" << interface.id
                      << ", instance=" << instance << std::endl;
        }
    };

    std::for_each(std::begin(result.current_clients), std::end(result.current_clients),
                  connect_client);

    notify_subscribed_callbacks(m_currently_running_service_report, callbacks_to_notify, interface,
                                instance, Find_result_status::added);

    auto inform_subscribers = [this, interface, instance]() {
        call_subscribe_find_service_callbacks(interface, instance, Find_result_status::deleted,
                                              true);
    };

    return std::make_unique<Registration_collection>(
        std::make_unique<Final_action_registration>(Final_action(std::move(inform_subscribers))),
        std::move(result.registration));
}

void Runtime_impl::stop_subscription(Find_subscription_id const& id) noexcept {
    std::unique_lock<std::mutex> lock{m_runtime_mutex};
    auto const callback = m_find_service_subscriptions.find(id);

    // Defensive programming. The true case will not occur because Runtime_impl implements
    // Stop_subscription which further calls stop_subscribe when Find_subscription_handle_impl gets
    // destroyed. This means the callback must be in the map. Furthermore, the function
    // stop_subscription itself cannot be tested as it is not an interface function.

    if (std::end(m_find_service_subscriptions) == callback) {
        return;
    }

    auto const& cb_id = callback->second;

    Find_result_callback_wptr const weak_cb{std::get<0>(cb_id)};

    auto const interface = std::get<1>(cb_id);
    auto const instance = std::get<2>(cb_id);

    Service_instance_to_callbacks& instances_to_callbacks = m_interface_to_callbacks.at(interface);
    Find_result_callbacks& callbacks = instances_to_callbacks.at(instance);
    cleanup(callbacks, std::get<0>(cb_id));
    cleanup(instances_to_callbacks, instance, callbacks);
    cleanup(m_interface_to_callbacks, interface, instances_to_callbacks);
    m_find_service_subscriptions.erase(callback);
    lock.unlock();

    // cleanup bridge find subscription
    if (interface) {
        std::lock_guard<std::mutex> const bridge_lock{m_bridge_mutex};
        auto const configuration = Service_interface_configuration{*interface};
        cleanup(m_active_bridge_find_services, std::make_tuple(configuration, instance));
    }

    // no data race, if run from same thread
    if (m_currently_running_service_report.data != std::this_thread::get_id()) {
        // sync deletion with running callbacks to ensure that all shared_ptr are deleted.
        std::lock_guard<std::mutex> const calling_callbacks_lock{
            m_currently_running_service_report.mutex};
    }
}

void Runtime_impl::stop_registration(Bridge_registration_id const& id) noexcept {
    std::unique_lock<std::mutex> bridge_lock{m_bridge_mutex};
    auto const provided_services = std::get<2>(m_bridge_to_callbacks.at(id));
    m_bridge_to_callbacks.erase(id);

    cleanup(m_active_bridge_find_services, id);
    cleanup(m_service_requests, id);
    bridge_lock.unlock();

    for (auto const& interface_with_instances : provided_services) {
        for (auto const& instance : interface_with_instances.second) {
            try {
                call_subscribe_find_service_callbacks(interface_with_instances.first, instance,
                                                      Find_result_status::deleted, false);
            }

            catch (...) {
            }
        }
    }
}

void Runtime_impl::update_bridges_provided_services(Bridge_registration_id const& bridge_id,
                                                    Service_interface const& interface,
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

void Runtime_impl::call_subscribe_find_service_callbacks(Service_interface const& interface,
                                                         Service_instance const& instance,
                                                         Find_result_status const status,
                                                         bool const local) {
    std::unique_lock<std::mutex> runtime_lock{m_runtime_mutex};
    auto const callbacks_to_notify =
        get_callbacks_to_notify(m_interface_to_callbacks, interface, instance, local);
    runtime_lock.unlock();
    notify_subscribed_callbacks(m_currently_running_service_report, callbacks_to_notify, interface,
                                instance, status);
}

Find_result_change_callback Runtime_impl::create_bridge_find_result_callback(
    Bridge_registration_id const& bridge_id) {
    assert(nullptr != bridge_id);
    return [this, bridge_id](Service_interface const& interface, Service_instance const& instance,
                             Find_result_status status) {
        update_bridges_provided_services(bridge_id, interface, instance, status);
        call_subscribe_find_service_callbacks(interface, instance, status, false);
    };
}

std::shared_ptr<Bridge_id_to_subscription> Runtime_impl::get_or_create_find_services(
    Service_interface const& interface, std::optional<Service_instance> const& instance,
    std::optional<Bridge_identity> identity) {
    auto const create_value = [this](auto const& callbacks, auto const& configuration,
                                     auto const& instance) {
        auto const notify_find_subscriptions =
            this->create_bridge_find_result_callback(callbacks.first);
        return std::get<0>(callbacks.second)(notify_find_subscriptions, configuration.interface,
                                             instance);
    };

    std::unique_lock<std::mutex> bridge_lock{m_bridge_mutex};
    return get_bridge_requests<Bridge_id_to_subscription>(
        Service_interface_configuration{interface}, instance, std::move(identity), bridge_lock,
        m_active_bridge_find_services, m_bridge_to_callbacks, create_value);
}

Registration Runtime_impl::bridge_service_requests(
    Service_interface_configuration const& configuration, Service_instance const& instance) {
    auto service_requests = get_or_create_service_requests(configuration, instance);

    auto remove_service_requests = [this, configuration, instance,
                                    service_requests = std::move(service_requests)]() mutable {
        service_requests.reset();
        remove_from_service_requests(configuration, instance);
    };

    Registration service_requests_handle = std::make_unique<Final_action_registration>(
        Final_action(std::move(remove_service_requests)));

    return service_requests_handle;
}

std::shared_ptr<Bridge_id_to_request> Runtime_impl::get_or_create_service_requests(
    Service_interface_configuration const& configuration, Service_instance const& instance) {
    auto const create_value = [](auto const& callbacks, auto const& configuration,
                                 auto const& instance) {
        return std::get<1>(callbacks.second)(configuration, instance);
    };
    std::unique_lock<std::mutex> bridge_lock{m_bridge_mutex};
    return get_bridge_requests<Bridge_id_to_request>(configuration, instance, {}, bridge_lock,
                                                     m_service_requests, m_bridge_to_callbacks,
                                                     create_value);
}

void Runtime_impl::remove_from_service_requests(
    Service_interface_configuration const& configuration, Service_instance const& instance) {
    std::lock_guard<std::mutex> const bridge_lock{m_bridge_mutex};
    cleanup(m_service_requests, std::make_tuple(configuration, instance));
}

Interfaces_instances Runtime_impl::get_bridge_reported_instances(
    Service_interface const& interface, std::optional<Service_instance> const& instance) const {
    std::lock_guard<std::mutex> const bridge_lock{m_bridge_mutex};
    Instances result;
    for (auto const& bridge_data : m_bridge_to_callbacks) {
        auto bridge_services = std::get<2>(bridge_data.second);
        auto const& instances = bridge_services.find(interface);
        if (std::end(bridge_services) != instances) {
            static_cast<void>(std::copy_if(std::begin(instances->second),
                                           std::end(instances->second), std::back_inserter(result),
                                           [&instance](Service_instance const& inst) {
                                               return is_matching_instance(instance, inst);
                                           }));
        }
    }
    return {{interface, result}};
}

Runtime::Uptr create_runtime() { return std::make_unique<Runtime_impl>(); }

}  // namespace socom
}  // namespace score
