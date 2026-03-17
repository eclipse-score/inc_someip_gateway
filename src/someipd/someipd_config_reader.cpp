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

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#include "src/someipd/someipd_config.h"

namespace score::someip_gateway::someipd {

namespace {

std::uint16_t ParseHex16(const std::string& value) {
    return static_cast<std::uint16_t>(std::stoul(value, nullptr, 16));
}

ServiceEventConfig ParseEvent(const nlohmann::json& obj) {
    return ServiceEventConfig{
        ParseHex16(obj.at("event_id").get<std::string>()),
        ParseHex16(obj.at("eventgroup_id").get<std::string>()),
    };
}

ServiceConfig ParseService(const nlohmann::json& obj) {
    ServiceConfig svc{};
    svc.service_id = ParseHex16(obj.at("service_id").get<std::string>());
    svc.instance_id = ParseHex16(obj.at("instance_id").get<std::string>());
    if (obj.contains("unreliable_port")) {
        svc.unreliable_port = obj.at("unreliable_port").get<std::uint16_t>();
    }
    for (const auto& ev : obj.at("events")) {
        svc.events.push_back(ParseEvent(ev));
    }
    return svc;
}

}  // namespace

SomeipDConfig ReadSomeipDConfig(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open someipd config file: " + config_path);
    }

    const nlohmann::json root = nlohmann::json::parse(file);

    SomeipDConfig config{};
    for (const auto& svc : root.at("offered_services")) {
        config.offered_services.push_back(ParseService(svc));
    }
    for (const auto& svc : root.at("subscribed_services")) {
        config.subscribed_services.push_back(ParseService(svc));
    }
    return config;
}

}  // namespace score::someip_gateway::someipd
