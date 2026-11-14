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

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "events/event_publisher.h"
#include "events/event_subscriber.h"
#include "events/event_types.h"
#include "gateway_sd_settings.h"
#include "opensomeip_host.h"
#include "routing.h"
#include "rpc/rpc_client.h"
#include "rpc/rpc_server.h"
#include "rpc/rpc_types.h"
#include "score/mw/log/logging.h"
#include "score/someip/someip_error.h"
#include "sd/sd_client.h"
#include "sd/sd_server.h"
#include "sd/sd_types.h"
#include "someip/types.h"
#include "transport/endpoint.h"
#include "transport/service_demux.h"
#include "transport/udp_transport.h"

namespace score::someipd {
// The library lives in the global someip namespace. An unqualified lookup would find score::someip.
namespace osi = ::someip;
namespace {

struct InstanceKey {
    std::uint16_t service_id{0};
    std::uint16_t instance_id{0};

    bool operator<(InstanceKey const& other) const {
        if (service_id != other.service_id) {
            return service_id < other.service_id;
        }
        return instance_id < other.instance_id;
    }
};

osi::sd::SdConfig make_sd_config(GatewaySdSettings const& settings) {
    osi::sd::SdConfig config;
    config.multicast_address = settings.multicast_address;
    config.multicast_port = (settings.multicast_port == 0) ? static_cast<std::uint16_t>(30490)
                                                           : settings.multicast_port;
    if (!settings.unicast_address.empty() && (settings.unicast_address != "0.0.0.0")) {
        config.unicast_address = settings.unicast_address;
    } else {
        config.unicast_address = "127.0.0.1";
    }
    config.unicast_port = config.multicast_port;
    config.initial_delay_min = std::chrono::milliseconds(settings.initial_delay_min_ms);
    config.initial_delay_max = std::chrono::milliseconds(settings.initial_delay_max_ms);
    // Match the max so pick_initial_wait_ms keeps the [min, max] span from the gateway file.
    config.initial_delay = config.initial_delay_max;
    config.repetition_base = std::chrono::milliseconds(settings.repetition_base_ms);
    config.repetition_multiplier = 2;
    // SdConfig ends the repetition phase when the next interval reaches cyclic_offer.
    // Base 200 ms and multiplier 2 reach 2000 ms after four offers (200, 400, 800, 1600).
    // vsomeip repetitions_max is a count of 3. That count stays on GatewaySdSettings; this
    // library has no count field.
    config.cyclic_offer = std::chrono::milliseconds(settings.cyclic_offer_ms);
    auto const ttl_seconds = (settings.ttl_seconds <= 0) ? 3 : settings.ttl_seconds;
    config.ttl =
        std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(ttl_seconds) * 1000);
    return config;
}

std::uint16_t take_client_id(std::uint16_t& next) {
    auto const id = next;
    next = static_cast<std::uint16_t>(next + 1U);
    if (next == 0U) {
        next = 1U;
    }
    return (id == 0U) ? take_client_id(next) : id;
}

std::vector<std::uint16_t> eventgroups_of(mw_someip_config::Event const& event) {
    std::vector<std::uint16_t> groups;
    auto const* const configured = event.eventgroup_ids();
    if ((configured != nullptr) && !configured->empty()) {
        for (auto const group_id : *configured) {
            groups.push_back(group_id);
        }
    } else {
        groups.push_back(event.event_id());
    }
    return groups;
}

}  // namespace

struct OpensomeipHost::Objects {
    GatewaySdSettings settings{};
    osi::sd::SdConfig sd_config{};
    std::string advertise_address;
    std::uint16_t next_client_id{1};
    bool running{false};
    std::mutex mutex;
    std::map<std::string, std::uint16_t> subscriber_ids;

    std::unique_ptr<osi::transport::UdpTransport> service_udp;
    std::unique_ptr<osi::transport::UdpTransport> sd_server_udp;
    std::unique_ptr<osi::transport::UdpTransport> sd_client_udp;
    std::unique_ptr<osi::transport::ServiceDemux> demux;
    std::unique_ptr<osi::sd::SdServer> sd_server;
    std::unique_ptr<osi::sd::SdClient> sd_client;

    std::map<std::uint16_t, std::unique_ptr<osi::rpc::RpcServer>> servers;
    std::map<InstanceKey, std::unique_ptr<osi::events::EventPublisher>> publishers;
    std::map<InstanceKey, std::unique_ptr<osi::rpc::RpcClient>> clients;
    std::map<InstanceKey, std::unique_ptr<osi::events::EventSubscriber>> subscribers;
    std::vector<std::unique_ptr<RemoteServiceRequest>> remotes;
};

OpensomeipHost::OpensomeipHost() : objects_(std::make_unique<Objects>()) {
    objects_->settings = load_gateway_sd_settings();
    objects_->sd_config = make_sd_config(objects_->settings);
    objects_->advertise_address = std::string(objects_->sd_config.unicast_address);

    osi::transport::UdpTransportConfig service_config;
    service_config.reuse_address = true;
    objects_->service_udp = std::make_unique<osi::transport::UdpTransport>(
        osi::transport::Endpoint("0.0.0.0", objects_->settings.service_port), service_config);
    objects_->demux = std::make_unique<osi::transport::ServiceDemux>(*objects_->service_udp);

    osi::transport::UdpTransportConfig sd_socket;
    sd_socket.reuse_address = true;
    sd_socket.reuse_port = true;
    if (!objects_->settings.unicast_address.empty() &&
        (objects_->settings.unicast_address != "0.0.0.0")) {
        sd_socket.multicast_interface = objects_->settings.unicast_address;
    }
    osi::transport::Endpoint const sd_bind("0.0.0.0", objects_->sd_config.multicast_port);
    objects_->sd_server_udp = std::make_unique<osi::transport::UdpTransport>(sd_bind, sd_socket);
    objects_->sd_client_udp = std::make_unique<osi::transport::UdpTransport>(sd_bind, sd_socket);
    objects_->sd_server =
        std::make_unique<osi::sd::SdServer>(objects_->sd_config, *objects_->sd_server_udp);
    objects_->sd_client =
        std::make_unique<osi::sd::SdClient>(objects_->sd_config, *objects_->sd_client_udp);

    objects_->sd_server->set_subscription_accepted_callback(
        [this](std::uint16_t service, std::uint16_t instance, std::uint16_t eventgroup,
               osi::transport::Endpoint const& subscriber,
               osi::sd::InitialDataDelivery initial_data) {
            on_subscription(service, instance, eventgroup, std::string(subscriber.get_address()),
                            subscriber.get_port(),
                            initial_data != osi::sd::InitialDataDelivery::SUPPRESSED);
        });
}

OpensomeipHost::~OpensomeipHost() { stop(); }

bool OpensomeipHost::start() {
    if (objects_->running) {
        return true;
    }
    if (objects_->demux->start() != osi::Result::SUCCESS) {
        score::mw::log::LogError() << "[someipd] Failed to start the shared service socket";
        return false;
    }
    if (!objects_->sd_server->initialize()) {
        score::mw::log::LogError() << "[someipd] Failed to start service discovery";
        static_cast<void>(objects_->demux->stop());
        return false;
    }
    if (!objects_->sd_client->initialize()) {
        score::mw::log::LogError()
            << "[someipd] Service discovery client failed to bind; offers still run";
    }
    {
        std::lock_guard const lock{objects_->mutex};
        objects_->running = true;
    }
    auto const& settings = objects_->settings;
    score::mw::log::LogInfo() << "[someipd] SD " << settings.multicast_address << ":"
                              << objects_->sd_config.multicast_port << " initial "
                              << settings.initial_delay_min_ms << "-"
                              << settings.initial_delay_max_ms << " ms, repetition base "
                              << settings.repetition_base_ms << " ms x" << settings.repetitions
                              << ", ttl " << settings.ttl_seconds << " s, cyclic "
                              << settings.cyclic_offer_ms << " ms, service port "
                              << objects_->service_udp->get_local_endpoint().get_port();
    for (auto const& remote : objects_->remotes) {
        arm_remote(*remote);
    }
    return true;
}

void OpensomeipHost::stop() {
    std::map<InstanceKey, std::unique_ptr<osi::events::EventPublisher>> publishers;
    std::map<std::uint16_t, std::unique_ptr<osi::rpc::RpcServer>> servers;
    std::map<InstanceKey, std::unique_ptr<osi::rpc::RpcClient>> clients;
    std::map<InstanceKey, std::unique_ptr<osi::events::EventSubscriber>> subscribers;
    {
        std::lock_guard const lock{objects_->mutex};
        objects_->running = false;
        publishers.swap(objects_->publishers);
        servers.swap(objects_->servers);
        clients.swap(objects_->clients);
        subscribers.swap(objects_->subscribers);
    }
    subscribers.clear();
    clients.clear();
    publishers.clear();
    servers.clear();
    if (objects_->sd_client) {
        objects_->sd_client->shutdown();
    }
    if (objects_->sd_server) {
        objects_->sd_server->shutdown();
    }
    if (objects_->demux) {
        static_cast<void>(objects_->demux->stop());
    }
}

void OpensomeipHost::on_subscription(std::uint16_t service_id, std::uint16_t instance_id,
                                     std::uint16_t eventgroup_id, std::string address,
                                     std::uint16_t port, bool send_initial) {
    std::lock_guard const lock{objects_->mutex};
    if (!objects_->running) {
        return;
    }
    auto const key = address + ":" + std::to_string(port);
    auto const existing = objects_->subscriber_ids.find(key);
    std::uint16_t client_id = 0;
    if (existing == objects_->subscriber_ids.end()) {
        client_id = take_client_id(objects_->next_client_id);
        objects_->subscriber_ids.emplace(key, client_id);
    } else {
        client_id = existing->second;
    }
    auto const publisher = objects_->publishers.find(InstanceKey{service_id, instance_id});
    if (publisher == objects_->publishers.end()) {
        score::mw::log::LogError()
            << "[someipd] Subscription for unknown service 0x"
            << score::mw::log::LogHex16{service_id} << " from " << address << ":" << port;
        return;
    }
    // The library reported this subscriber. The allow-list stays in someipd; nothing in the
    // flatbuffer rejects it, so the publisher records the endpoint and sends from here.
    publisher->second->set_default_client_endpoint(address, port);
    auto const ttl = static_cast<std::uint32_t>(
        (objects_->settings.ttl_seconds <= 0) ? 3 : objects_->settings.ttl_seconds);
    osi::platform::Vector<osi::events::EventFilter> filters;
    static_cast<void>(publisher->second->handle_subscription(eventgroup_id, client_id, ttl, filters,
                                                             send_initial));
}

void OpensomeipHost::offer(mw_someip_config::Root const& config) {
    auto const port = objects_->service_udp->get_local_endpoint().get_port();
    auto const unicast = objects_->advertise_address + ":" + std::to_string(port);
    for (auto const* const service_type : *config.service_types()) {
        auto const* const instances = service_type->local_service_instances();
        if ((instances == nullptr) || instances->empty()) {
            continue;
        }
        auto const service_id = service_type->service_id();
        auto const major = service_type->service_version_major();
        {
            std::lock_guard const lock{objects_->mutex};
            if (objects_->servers.find(service_id) == objects_->servers.end()) {
                auto server =
                    std::make_unique<osi::rpc::RpcServer>(service_id, *objects_->demux, major);
                if (!server->initialize()) {
                    score::mw::log::LogError()
                        << "[someipd] Failed to register service 0x"
                        << score::mw::log::LogHex16{service_id} << " on the shared demux";
                } else if (service_type->methods() != nullptr) {
                    for (auto const* const method : *service_type->methods()) {
                        auto const method_id = method->method_id();
                        static_cast<void>(server->register_method(
                            method_id,
                            [](std::uint16_t, std::uint16_t, osi::platform::ByteBuffer const& input,
                               osi::platform::ByteBuffer& output) {
                                output = input;
                                return osi::rpc::RpcResult::SUCCESS;
                            }));
                    }
                }
                objects_->servers.emplace(service_id, std::move(server));
            }
        }
        for (auto const* const instance : *instances) {
            auto const instance_id = instance->instance_id();
            osi::platform::Vector<std::uint16_t> groups;
            std::set<std::uint16_t> unique_groups;
            {
                std::lock_guard const lock{objects_->mutex};
                auto publisher =
                    std::make_unique<osi::events::EventPublisher>(service_id, instance_id, major);
                if (!publisher->initialize()) {
                    score::mw::log::LogError()
                        << "[someipd] Failed to start event publisher for service 0x"
                        << score::mw::log::LogHex16{service_id};
                    continue;
                }
                if (service_type->events() != nullptr) {
                    for (auto const* const event : *service_type->events()) {
                        auto const event_groups = eventgroups_of(*event);
                        osi::events::EventConfig event_config;
                        event_config.event_id = event->event_id();
                        event_config.eventgroup_id =
                            event_groups.empty() ? event->event_id() : event_groups.front();
                        event_config.reliability = osi::events::Reliability::UNRELIABLE;
                        event_config.is_field = false;
                        static_cast<void>(publisher->register_event(event_config));
                        for (auto const group_id : event_groups) {
                            unique_groups.insert(group_id);
                        }
                    }
                }
                objects_->publishers.insert_or_assign(InstanceKey{service_id, instance_id},
                                                      std::move(publisher));
            }
            for (auto const group_id : unique_groups) {
                groups.push_back(group_id);
            }
            osi::sd::ServiceInstance offered(service_id, instance_id, major,
                                             service_type->service_version_minor());
            offered.protocol = 0x11;
            if (!objects_->sd_server->offer_service(offered, unicast, "", groups)) {
                score::mw::log::LogError() << "[someipd] Failed to offer service 0x"
                                           << score::mw::log::LogHex16{service_id} << " instance 0x"
                                           << score::mw::log::LogHex16{instance_id};
                continue;
            }
            score::mw::log::LogInfo()
                << "[someipd] Offering service 0x" << score::mw::log::LogHex16{service_id}
                << " instance 0x" << score::mw::log::LogHex16{instance_id} << " at " << unicast;
        }
    }
}

void OpensomeipHost::publish_event(std::uint16_t service_id, std::uint16_t instance_id,
                                   std::uint16_t event_id,
                                   osi::platform::ByteBuffer const& payload) {
    std::lock_guard const lock{objects_->mutex};
    if (!objects_->running) {
        return;
    }
    auto const publisher = objects_->publishers.find(InstanceKey{service_id, instance_id});
    if (publisher == objects_->publishers.end()) {
        score::mw::log::LogError()
            << "[someipd] Dropping event 0x" << score::mw::log::LogHex16{event_id} << ": service 0x"
            << score::mw::log::LogHex16{service_id} << " is not offered";
        return;
    }
    static_cast<void>(publisher->second->publish_event(event_id, payload));
}

void OpensomeipHost::arm_remote(RemoteServiceRequest& request) {
    if (request.armed || !objects_->running || (objects_->sd_client == nullptr) ||
        !objects_->sd_client->is_ready()) {
        return;
    }
    request.armed = true;
    auto const service_id = request.service_id;
    auto const instance_id = request.instance_id;
    static_cast<void>(objects_->sd_client->subscribe_service(
        service_id,
        [this, &request, instance_id](osi::sd::ServiceInstance const& found) {
            if (found.instance_id != instance_id) {
                return;
            }
            on_remote_found(request, std::string(found.ip_address), found.port);
        },
        [](osi::sd::ServiceInstance const&) {}));
    static_cast<void>(objects_->sd_client->find_service(
        service_id, [](osi::platform::Vector<osi::sd::ServiceInstance> const&) {},
        std::chrono::milliseconds(0)));
}

void OpensomeipHost::on_remote_found(RemoteServiceRequest& request, std::string const& address,
                                     std::uint16_t port) {
    if ((port == 0) || address.empty()) {
        score::mw::log::LogError()
            << "[someipd] Offer for service 0x" << score::mw::log::LogHex16{request.service_id}
            << " has no endpoint";
        return;
    }
    osi::transport::Endpoint const remote(address, port);
    std::uint16_t client_id = 0;
    osi::rpc::RpcClient* client = nullptr;
    osi::events::EventSubscriber* subscriber = nullptr;
    RemoteEventHandler handler;
    std::uint16_t first_group = 0;
    bool subscribe_events = false;
    {
        std::lock_guard const lock{objects_->mutex};
        if (!objects_->running) {
            return;
        }
        auto const key = InstanceKey{request.service_id, request.instance_id};
        auto const existing = objects_->clients.find(key);
        if (existing == objects_->clients.end()) {
            client_id = take_client_id(objects_->next_client_id);
            auto created = std::make_unique<osi::rpc::RpcClient>(client_id, request.major_version);
            if (!created->initialize()) {
                score::mw::log::LogError() << "[someipd] Failed to start RPC client for service 0x"
                                           << score::mw::log::LogHex16{request.service_id};
                return;
            }
            created->set_remote_endpoint(remote);
            client = created.get();
            objects_->clients.emplace(key, std::move(created));
        } else {
            existing->second->set_remote_endpoint(remote);
            client = existing->second.get();
        }
        static_cast<void>(client);
        if (!request.eventgroup_ids.empty() &&
            (objects_->subscribers.find(key) == objects_->subscribers.end())) {
            auto const subscriber_id = take_client_id(objects_->next_client_id);
            auto created = std::make_unique<osi::events::EventSubscriber>(subscriber_id);
            if (!created->initialize()) {
                score::mw::log::LogError()
                    << "[someipd] Failed to start event subscriber for service 0x"
                    << score::mw::log::LogHex16{request.service_id};
            } else {
                created->set_default_endpoint(address, port);
                first_group = request.eventgroup_ids.front();
                handler = request.on_event;
                subscriber = created.get();
                subscribe_events = true;
                objects_->subscribers.emplace(key, std::move(created));
            }
        }
    }
    if (subscribe_events && (subscriber != nullptr)) {
        ObservedSender sender;
        sender.address = address;
        sender.port = port;
        auto const service_id = request.service_id;
        auto const instance_id = request.instance_id;
        static_cast<void>(subscriber->subscribe_eventgroup(
            service_id, instance_id, first_group,
            [handler, sender](osi::events::EventNotification const& notification) {
                ObservedEvent event;
                event.service_id = notification.service_id;
                event.event_id = notification.event_id;
                event.client_id = notification.client_id;
                event.session_id = notification.session_id;
                event.protocol_version = osi::SOMEIP_PROTOCOL_VERSION;
                event.interface_version = notification.interface_version;
                event.message_type = static_cast<std::uint8_t>(notification.message_type);
                event.return_code = static_cast<std::uint8_t>(notification.return_code);
                handler(event, notification.event_data, sender);
            }));
    }
    for (auto const group_id : request.eventgroup_ids) {
        static_cast<void>(objects_->sd_client->subscribe_eventgroup(request.service_id,
                                                                    request.instance_id, group_id));
    }
}

void OpensomeipHost::subscribe_remote(RemoteServiceRequest request) {
    auto stored = std::make_unique<RemoteServiceRequest>(std::move(request));
    RemoteServiceRequest* armed = nullptr;
    {
        std::lock_guard const lock{objects_->mutex};
        objects_->remotes.push_back(std::move(stored));
        armed = objects_->remotes.back().get();
    }
    arm_remote(*armed);
}

bool OpensomeipHost::call_method(std::uint16_t service_id, std::uint16_t instance_id,
                                 std::uint16_t method_id,
                                 osi::platform::ByteBuffer const& payload) {
    osi::rpc::RpcClient* client = nullptr;
    {
        std::lock_guard const lock{objects_->mutex};
        auto const existing = objects_->clients.find(InstanceKey{service_id, instance_id});
        if (existing == objects_->clients.end()) {
            score::mw::log::LogError()
                << "[someipd] No RPC client for service 0x" << score::mw::log::LogHex16{service_id};
            return false;
        }
        client = existing->second.get();
    }
    auto const result = client->call_method_sync(service_id, method_id, payload);
    if (result.result != osi::rpc::RpcResult::SUCCESS) {
        score::mw::log::LogError()
            << "[someipd] Method 0x" << score::mw::log::LogHex16{method_id} << " on service 0x"
            << score::mw::log::LogHex16{service_id} << " failed";
        return false;
    }
    return true;
}

Routing::~Routing() = default;

OpensomeipHost& Routing::host() noexcept { return *host_; }

Result<std::unique_ptr<Routing>> Routing::Create() {
    std::unique_ptr<Routing> routing{new Routing{}};
    routing->host_ = std::make_unique<OpensomeipHost>();
    score::mw::log::LogInfo() << "[someipd] opensomeip stack initialized";
    return routing;
}

void Routing::SetupOfferings(score::mw_someip_config::Root const& config) { host_->offer(config); }

void Routing::AddServices(std::shared_ptr<const score::mw_someip_config::Root> config,
                          std::function<void()> on_registered) {
    Services added{std::move(config), std::move(on_registered)};
    {
        std::lock_guard const lock{mutex_};
        services_.push_back(added);
        if (!registered_) {
            return;
        }
    }
    SetupOfferings(*added.config);
    added.on_registered();
}

void Routing::Run(const std::function<void()>& wait_for_shutdown) {
    score::mw::log::LogInfo() << "[someipd] Starting network stack processing...";
    if (!host_->start()) {
        score::mw::log::LogFatal() << "[someipd] Network stack initialization failed";
        return;
    }
    std::vector<Services> services{};
    {
        std::lock_guard const lock{mutex_};
        registered_ = true;
        services = services_;
    }
    score::mw::log::LogInfo() << "[someipd] Setting up offerings...";
    for (auto const& added : services) {
        SetupOfferings(*added.config);
        added.on_registered();
    }
    score::mw::log::LogInfo() << "[someipd] Network stack started, waiting for shutdown.";
    wait_for_shutdown();
    score::mw::log::LogInfo() << "[someipd] Stopping network stack processing...";
    host_->stop();
    {
        std::lock_guard const lock{mutex_};
        registered_ = false;
    }
    score::mw::log::LogInfo() << "[someipd] Network stack stopped.";
}

}  // namespace score::someipd
