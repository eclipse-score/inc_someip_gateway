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

#include "local_service_instance.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

#include "score/mw/com/com_error_domain.h"
#include "score/mw/com/types.h"
#include "score/mw/log/logging.h"
#include "score/serializer/serializer.h"
#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"

using score::mw::com::GenericProxy;
using score::mw::com::SamplePtr;

namespace score::someip_gateway::gatewayd {

namespace {

constexpr std::size_t kSomeipLengthExcludedHeaderBytes{8U};
constexpr std::size_t kMaxSomeipSerializedPayloadSize{
    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) -
    kSomeipLengthExcludedHeaderBytes};
constexpr std::uint16_t kDefaultSomeipClientId{0xFFFFU};
constexpr std::uint16_t kDefaultSomeipSessionId{0x0000U};
constexpr std::uint8_t kSomeipProtocolVersion{0x01U};
constexpr std::uint8_t kSomeipNotificationMessageType{0x02U};
constexpr std::uint8_t kSomeipReturnCodeOk{0x00U};
constexpr unsigned kBitsPerByte{8U};
constexpr unsigned kTwoBytesBits{16U};
constexpr unsigned kThreeBytesBits{24U};
constexpr std::uint32_t kByteMask{0xFFU};

bool is_valid_local_config(const mw_someip_config::ServiceInstance* service_instance_config,
                           const mw_someip_config::ServiceType* service_type_config) noexcept {
    if ((service_instance_config == nullptr) ||
        (service_instance_config->instance_specifier() == nullptr) ||
        (service_type_config == nullptr) || (service_type_config->service_type_name() == nullptr) ||
        (service_type_config->events() == nullptr) ||
        (service_type_config->events()->size() >
         static_cast<std::size_t>(std::numeric_limits<socom::Event_id>::max()))) {
        return false;
    }
    for (const auto* event_config : *service_type_config->events()) {
        if ((event_config == nullptr) || (event_config->event_name() == nullptr)) {
            return false;
        }
    }
    return true;
}

std::mutex& discovery_instances_mutex() noexcept {
    static std::mutex mutex{};
    return mutex;
}

struct FindServiceContext {
    std::shared_ptr<const mw_someip_config::ServiceInstance> config;
    std::shared_ptr<const mw_someip_config::ServiceType> service_config;
    socom::Runtime* socom_runtime;
    std::vector<std::unique_ptr<LocalServiceInstance>>* instances;
    std::mutex mutex{};
    bool completed{false};

    FindServiceContext(std::shared_ptr<const mw_someip_config::ServiceInstance> config_,
                       std::shared_ptr<const mw_someip_config::ServiceType> service_config_,
                       socom::Runtime& socom_runtime_,
                       std::vector<std::unique_ptr<LocalServiceInstance>>& instances_) noexcept
        : config(std::move(config_)),
          service_config(std::move(service_config_)),
          socom_runtime(&socom_runtime_),
          instances(&instances_) {}
};

}  // namespace

LocalServiceInstance::LocalServiceInstance(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    socom::Enabled_server_connector::Uptr server_connector, GenericProxy&& ipc_proxy)
    : service_instance_config_(std::move(service_instance_config)),
      service_type_config_(std::move(service_type_config)),
      server_connector_(std::move(server_connector)),
      event_contexts_{},
      ipc_proxy_(std::move(ipc_proxy)) {}

LocalServiceInstance::~LocalServiceInstance() {
    for (auto& event_context : event_contexts_) {
        if (event_context.subscribed && (event_context.ipc_event != nullptr)) {
            if (const auto unset_result = event_context.ipc_event->UnsetReceiveHandler();
                !unset_result) {
                score::mw::log::LogError()
                    << "[gatewayd] LocalServiceInstance - Failed to unset receive handler for "
                       "event "
                    << event_context.socom_event_id << ": " << unset_result.error().Message();
            }
            event_context.ipc_event->Unsubscribe();
            event_context.subscribed = false;
        }
    }
}

Result<std::unique_ptr<LocalServiceInstance>> LocalServiceInstance::Create(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    GenericProxy&& ipc_proxy, socom::Runtime& socom_runtime) {
    if (!is_valid_local_config(service_instance_config.get(), service_type_config.get())) {
        score::mw::log::LogError()
            << "[gatewayd] Invalid configuration passed to LocalServiceInstance::Create";
        return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
    }

    auto const service_type_name = service_type_config->service_type_name()->string_view();
    socom::Service_interface_identifier const iface{
        service_type_name,
        {service_type_config->service_version_major(),
         static_cast<std::uint16_t>(service_type_config->service_version_minor())}};

    socom::Service_instance const inst{service_type_name};

    socom::Server_service_interface_definition const server_config{
        iface, socom::to_num_of_methods(0U),
        socom::to_num_of_events(service_type_config->events()->size())};

    auto disabled_server_connector = socom_runtime.make_server_connector(
        server_config, inst,
        {
            .on_method_call = [](socom::Enabled_server_connector&, socom::Method_id, socom::Payload,
                                 socom::Method_call_reply_data_opt)
                -> socom::Method_invocation::Uptr { return nullptr; },
            .on_event_subscription_change = [](socom::Enabled_server_connector&, socom::Event_id,
                                               socom::Event_state) {},
            .on_event_update_request = [](socom::Enabled_server_connector&, socom::Event_id) {},
            .on_method_call_payload_allocate =
                [](socom::Enabled_server_connector&,
                   socom::Method_id) -> score::Result<socom::Writable_payload> {
                return MakeUnexpected(socom::Error::runtime_error_request_rejected);
            },
        });

    if (!disabled_server_connector.has_value()) {
        score::mw::log::LogError()
            << "[gatewayd] Failed to create server connector for '" << service_type_name << "'";
        return MakeUnexpected(socom::Error::runtime_error_request_rejected);
    }
    std::cout << "[gatewayd] LocalServiceInstance - Enabled server_connector for "
              << service_type_name << std::endl;
    auto server_connector =
        socom::Disabled_server_connector::enable(std::move(disabled_server_connector).value());

    auto instance = std::unique_ptr<LocalServiceInstance>(
        new LocalServiceInstance(std::move(service_instance_config), std::move(service_type_config),
                                 std::move(server_connector), std::move(ipc_proxy)));

    auto events = instance->ipc_proxy_.GetEvents();
    auto const* const configured_events = instance->service_type_config_->events();
    instance->event_contexts_.reserve(configured_events->size());

    socom::Event_id socom_event_id{0U};
    for (const auto* event_config : *configured_events) {
        auto const event_name = event_config->event_name()->string_view();
        auto result = events.find(event_name);
        if (result == events.cend()) {
            score::mw::log::LogWarn()
                << "[gatewayd] Failed to find " << event_name << " event in generic ipc_proxy.";
            ++socom_event_id;
            continue;
        }
        auto& ipc_event = result->second;

        const score_com_serializer* serializer = nullptr;
        auto const get_result =
            score_com_serializer_get(service_type_name.data(), service_type_name.size(),
                                     score_com_serializer_element_type_event, event_name.data(),
                                     event_name.size(), &serializer);
        if ((get_result != score_com_serializer_result_ok) || (serializer == nullptr)) {
            score::mw::log::LogError() << "[gatewayd] Failed to get serializer for "
                                       << service_type_name << "::" << event_name;
            ++socom_event_id;
            continue;
        }

        instance->event_contexts_.push_back(
            EventContext{event_config, serializer, socom_event_id, &ipc_event, false});
        ++socom_event_id;
    }

    for (auto& event_context : instance->event_contexts_) {
        auto& ipc_event = *event_context.ipc_event;
        auto receive_handler = [instance_ptr = instance.get(), &ipc_event, &event_context]() {
            if (const auto get_samples_result = ipc_event.GetNewSamples(
                    create_process_sample_callback(instance_ptr, event_context),
                    someip::kMaxSampleCount);
                !get_samples_result) {
                score::mw::log::LogError()
                    << "[gatewayd] LocalServiceInstance - Failed to read samples for event "
                    << event_context.socom_event_id << ": " << get_samples_result.error().Message();
            }
        };

        if (const auto set_handler_result = ipc_event.SetReceiveHandler(std::move(receive_handler));
            !set_handler_result) {
            score::mw::log::LogError()
                << "[gatewayd] LocalServiceInstance - Failed to set receive handler for event "
                << event_context.socom_event_id << ": " << set_handler_result.error().Message();
            continue;
        }

        if (const auto subscribe_result = ipc_event.Subscribe(someip::kMaxSampleCount);
            !subscribe_result) {
            if (const auto unset_result = ipc_event.UnsetReceiveHandler(); !unset_result) {
                score::mw::log::LogError()
                    << "[gatewayd] LocalServiceInstance - Failed to unset receive handler after "
                       "subscribe failure for event "
                    << event_context.socom_event_id << ": " << unset_result.error().Message();
            }
            score::mw::log::LogError()
                << "[gatewayd] LocalServiceInstance - Failed to subscribe event "
                << event_context.socom_event_id << ": " << subscribe_result.error().Message();
            continue;
        }

        event_context.subscribed = true;
    }

    return instance;
}

score::cpp::move_only_function<void(SamplePtr<void>)>
LocalServiceInstance::create_process_sample_callback(LocalServiceInstance* instance_ptr,
                                                     EventContext const& event_context) {
    return [instance_ptr, &event_context](SamplePtr<void> sample) {
        if (sample.get() == nullptr) {
            score::mw::log::LogError()
                << "[gatewayd] LocalServiceInstance - Received null sample for event "
                << event_context.socom_event_id;
            return;
        }

        auto maybe_payload =
            instance_ptr->server_connector_->allocate_event_payload(event_context.socom_event_id);
        if (!maybe_payload.has_value()) {
            score::mw::log::LogError()
                << "[gatewayd] LocalServiceInstance - Failed to allocate event payload for event "
                << event_context.socom_event_id << ": " << maybe_payload.error().Message();
            return;
        }

        auto& payload = *maybe_payload;
        auto const wdata = payload.wdata();
        if (wdata.size() < someip::kSomeipFullHeaderSize) {
            score::mw::log::LogError()
                << "[gatewayd] LocalServiceInstance - Allocated payload size (" << wdata.size()
                << ") is smaller than SOME/IP header size for event "
                << event_context.socom_event_id;
            return;
        }

        auto const payload_span = wdata.subspan(someip::kSomeipFullHeaderSize);
        std::size_t written_length{0U};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        auto const serialize_result = score_com_serializer_serialize(
            event_context.serializer, reinterpret_cast<std::uint8_t*>(payload_span.data()),
            payload_span.size(), sample.get(), &written_length);
        if (serialize_result != score_com_serializer_result_ok) {
            score::mw::log::LogError() << "[gatewayd] Serialization failed for "
                                       << event_context.config->event_name()->string_view();
            return;
        }

        if ((written_length > payload_span.size()) ||
            (written_length >
             (std::numeric_limits<std::size_t>::max() - someip::kSomeipFullHeaderSize)) ||
            (written_length > kMaxSomeipSerializedPayloadSize)) {
            score::mw::log::LogError() << "[gatewayd] Serialized length out of bounds for "
                                       << event_context.config->event_name()->string_view();
            return;
        }

        std::uint32_t const someip_length =
            static_cast<std::uint32_t>(kSomeipLengthExcludedHeaderBytes + written_length);
        std::size_t const total_size = someip::kSomeipFullHeaderSize + written_length;

        std::size_t pos{0U};
        std::uint16_t const service_id = instance_ptr->service_type_config_->service_id();
        wdata[pos++] = static_cast<std::byte>((service_id >> kBitsPerByte) & kByteMask);
        wdata[pos++] = static_cast<std::byte>(service_id & kByteMask);

        std::uint16_t const method_id = event_context.config->event_id();
        wdata[pos++] = static_cast<std::byte>((method_id >> kBitsPerByte) & kByteMask);
        wdata[pos++] = static_cast<std::byte>(method_id & kByteMask);

        wdata[pos++] = static_cast<std::byte>((someip_length >> kThreeBytesBits) & kByteMask);
        wdata[pos++] = static_cast<std::byte>((someip_length >> kTwoBytesBits) & kByteMask);
        wdata[pos++] = static_cast<std::byte>((someip_length >> kBitsPerByte) & kByteMask);
        wdata[pos++] = static_cast<std::byte>(someip_length & kByteMask);

        wdata[pos++] = static_cast<std::byte>((kDefaultSomeipClientId >> kBitsPerByte) & kByteMask);
        wdata[pos++] = static_cast<std::byte>(kDefaultSomeipClientId & kByteMask);

        wdata[pos++] =
            static_cast<std::byte>((kDefaultSomeipSessionId >> kBitsPerByte) & kByteMask);
        wdata[pos++] = static_cast<std::byte>(kDefaultSomeipSessionId & kByteMask);

        wdata[pos++] = static_cast<std::byte>(kSomeipProtocolVersion);
        wdata[pos++] =
            static_cast<std::byte>(instance_ptr->service_type_config_->service_version_major());
        wdata[pos++] = static_cast<std::byte>(kSomeipNotificationMessageType);
        wdata[pos++] = static_cast<std::byte>(kSomeipReturnCodeOk);

        if (!payload.shrink(total_size)) {
            score::mw::log::LogError()
                << "[gatewayd] LocalServiceInstance - Failed to shrink payload for event "
                << event_context.socom_event_id;
            return;
        }

        if (const auto result = instance_ptr->server_connector_->update_event(
                event_context.socom_event_id, std::move(payload));
            !result) {
            score::mw::log::LogError()
                << "[gatewayd] LocalServiceInstance - Failed to update event "
                << event_context.socom_event_id << ": " << result.error().Message();
        }
    };
}

Result<mw::com::FindServiceHandle> LocalServiceInstance::CreateAsyncLocalServices(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    socom::Runtime& socom_runtime, std::vector<std::unique_ptr<LocalServiceInstance>>& instances) {
    if (!is_valid_local_config(service_instance_config.get(), service_type_config.get())) {
        score::mw::log::LogError()
            << "[gatewayd] Invalid configuration passed to CreateAsyncLocalServices";
        return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
    }

    auto instance_specifier_result = score::mw::com::InstanceSpecifier::Create(
        service_instance_config->instance_specifier()->str());
    if (!instance_specifier_result.has_value()) {
        score::mw::log::LogError()
            << "[gatewayd] Invalid instance specifier '"
            << service_instance_config->instance_specifier()->string_view() << "'";
        return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
    }

    auto context = std::make_shared<FindServiceContext>(std::move(service_instance_config),
                                                        std::move(service_type_config),
                                                        socom_runtime, instances);

    return GenericProxy::StartFindService(
        [context = std::move(context)](auto handles, auto find_handle) {
            if (handles.empty()) {
                return;
            }

            auto const local_context = context;
            auto const specifier_view = local_context->config->instance_specifier()->string_view();
            {
                std::lock_guard<std::mutex> const discovery_lock{local_context->mutex};
                if (local_context->completed) {
                    return;
                }

                auto proxy_result = GenericProxy::Create(handles.front());
                if (!proxy_result.has_value()) {
                    score::mw::log::LogError()
                        << "[gatewayd] Proxy creation failed for instance specifier: '"
                        << specifier_view << "': " << proxy_result.error().Message();
                    return;
                }

                auto create_result = LocalServiceInstance::Create(
                    local_context->config, local_context->service_config,
                    std::move(proxy_result).value(), *local_context->socom_runtime);
                if (!create_result.has_value()) {
                    score::mw::log::LogError()
                        << "[gatewayd] Failed to create LocalServiceInstance for '"
                        << specifier_view << "'";
                    return;
                }

                {
                    std::lock_guard<std::mutex> const instances_lock{discovery_instances_mutex()};
                    local_context->instances->push_back(std::move(create_result).value());
                }
                local_context->completed = true;
            }

            std::cout << "[gatewayd] LocalServiceInstance created for instance specifier: "
                      << specifier_view << std::endl;

            if (const auto stop_result = GenericProxy::StopFindService(find_handle); !stop_result) {
                score::mw::log::LogError()
                    << "[gatewayd] Failed to stop find service for instance specifier: "
                    << specifier_view << ", error: " << stop_result.error().Message();
            }
        },
        std::move(instance_specifier_result).value());
}

}  // namespace score::someip_gateway::gatewayd
