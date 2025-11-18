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
#include "sample_sender_receiver.h"

#include "score/concurrency/notification.h"
#include "score/mw/com/impl/handle_type.h"
#include "score/mw/com/impl/proxy_event.h"
#include <score/assert.hpp>
#include <score/gateway/common.h>
#include <score/gateway/datatype.h>
#include <score/hash.hpp>
#include <score/optional.hpp>

#include <cstring>
#include <iostream>
#include <thread>
#include <type_traits>
#include <utility>

using namespace std::chrono_literals;
using score::mw::com::InstanceSpecifier;
using score::mw::com::SamplePtr;
using score::mw::com::impl::HandleType;
using score::mw::com::impl::ProxyEvent;
using score::mw::com::impl::ServiceHandleContainer;

namespace score::gateway {

namespace {

class SampleReceiver {
public:
    explicit SampleReceiver(const InstanceSpecifier& instance_specifier)
        : instance_specifier_{instance_specifier}, received_{0U}
    {
    }

    void ReceiveSample(const MapApiLanesStamped& map) noexcept
    {
        std::cout << ToString(instance_specifier_, ": Received sample no: ", received_,
                              ", with content: ", map.string_data.data(), "\n");
        received_ += 1U;
    }

private:
    const score::mw::com::InstanceSpecifier& instance_specifier_;
    std::size_t received_;
};

score::cpp::optional<std::reference_wrapper<ProxyEvent<MapApiLanesStamped>>>
GetMapApiLanesStampedProxyEvent(IpcBridgeProxy& proxy)
{
    return proxy.map_api_lanes_stamped_;
}

score::Result<HandleType> GetHandleFromSpecifier(const InstanceSpecifier& instance_specifier)
{
    std::cout << ToString(instance_specifier, ": Running as proxy, looking for services\n");
    ServiceHandleContainer<HandleType> handles{};
    do {
        auto handles_result = IpcBridgeProxy::FindService(instance_specifier);
        if (!handles_result.has_value()) {
            return MakeUnexpected<HandleType>(std::move(handles_result.error()));
        }
        handles = std::move(handles_result).value();
        if (handles.size() == 0) {
            std::this_thread::sleep_for(10ms);
        }
    } while (handles.size() == 0);

    std::cout << ToString(instance_specifier, ": Found service, instantiating proxy\n");
    return handles.front();
}

} // namespace

int EventSenderReceiver::RunAsProxy(
    const score::mw::com::InstanceSpecifier& instance_specifier,
    const score::cpp::optional<std::chrono::milliseconds> cycle_time, const std::size_t num_cycles)
{
    constexpr std::size_t SAMPLES_PER_CYCLE = 2U;

    auto handle_result = GetHandleFromSpecifier(instance_specifier);
    if (!handle_result.has_value()) {
        std::cerr << "Unable to find service: " << instance_specifier
                  << ". Failed with error: " << handle_result.error() << ", bailing!\n";
        return EXIT_FAILURE;
    }
    auto handle = handle_result.value();

    auto proxy_result = IpcBridgeProxy::Create(std::move(handle));
    if (!proxy_result.has_value()) {
        std::cerr << "Unable to construct proxy: " << proxy_result.error() << ", bailing!\n";
        return EXIT_FAILURE;
    }
    auto& proxy = proxy_result.value();

    auto map_api_lanes_stamped_event_optional = GetMapApiLanesStampedProxyEvent(proxy);
    if (!map_api_lanes_stamped_event_optional.has_value()) {
        std::cerr << "Could not get MapApiLanesStamped proxy event\n";
        return EXIT_FAILURE;
    }
    auto& map_api_lanes_stamped_event = map_api_lanes_stamped_event_optional.value().get();

    concurrency::Notification event_received;
    if (!cycle_time.has_value()) {
        map_api_lanes_stamped_event.SetReceiveHandler([&event_received, &instance_specifier]() {
            std::cout << ToString(instance_specifier, ": Callback called\n");
            event_received.notify();
        });
    }

    std::cout << ToString(instance_specifier, ": Subscribing to service\n");
    map_api_lanes_stamped_event.Subscribe(SAMPLES_PER_CYCLE);

    score::cpp::optional<char> last_received{};
    SampleReceiver receiver{instance_specifier};
    for (std::size_t cycle = 0U; cycle < num_cycles;) {
        const auto cycle_start_time = std::chrono::steady_clock::now();
        if (cycle_time.has_value()) {
            std::this_thread::sleep_for(*cycle_time);
        }

        Result<std::size_t> num_samples_received = map_api_lanes_stamped_event.GetNewSamples(
            [&receiver](SamplePtr<MapApiLanesStamped> sample) noexcept {
                // For the GenericProxy case, the void pointer managed by the SamplePtr<void> will
                // be cast to MapApiLanesStamped.
                const MapApiLanesStamped& sample_value = *sample.get();
                receiver.ReceiveSample(sample_value);
            },
            SAMPLES_PER_CYCLE);

        if (*num_samples_received >= 1U) {
            std::cout << ToString(instance_specifier, ": Proxy received valid data\n");
            cycle += *num_samples_received;
        }

        const auto cycle_duration = std::chrono::steady_clock::now() - cycle_start_time;

        std::cout << ToString(
            instance_specifier, ": Cycle duration ",
            std::chrono::duration_cast<std::chrono::milliseconds>(cycle_duration).count(), "ms\n");

        event_received.reset();
    }

    std::cout << ToString(instance_specifier, ": Unsubscribing...\n");
    map_api_lanes_stamped_event.Unsubscribe();
    std::cout << ToString(instance_specifier, ": and terminating, bye bye\n");
    return EXIT_SUCCESS;
}

} // namespace score::gateway
