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

#ifndef SCORE_GATEWAYD_TEST_SERVICE_INSTANCE_TEST_FIXTURE_H
#define SCORE_GATEWAYD_TEST_SERVICE_INSTANCE_TEST_FIXTURE_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "flatbuffers/flatbuffers.h"
#include "score/config/mw_someip_config_generated.h"
#include "score/gateway_ipc_binding/test/unit/mw_com_mocks.hpp"
#include "score/mw/com/impl/handle_type.h"
#include "score/result/result.h"
#include "score/serializer/serializer.h"
#include "score/socom/runtime_mock.hpp"

namespace score::someip_gateway::gatewayd {

inline constexpr std::string_view kServiceTypeName{"echo_service"};
inline constexpr std::uint16_t kServiceId{0x1234U};
inline constexpr std::uint8_t kServiceMajorVersion{1U};
inline constexpr std::uint32_t kServiceMinorVersion{0U};
inline constexpr std::string_view kLocalSpecifier{"gatewayd/local_echo"};
inline constexpr std::string_view kRemoteSpecifier{"gatewayd/remote_echo"};
inline constexpr std::uint16_t kInstanceId{1U};

inline constexpr std::string_view kEventSerialized{"ev_serialized"};
inline constexpr std::uint16_t kEventSerializedId{0x8001U};
inline constexpr std::uint32_t kMaxMessageSize{32U};

inline constexpr std::string_view kEventNoSerializer{"ev_no_serializer"};
inline constexpr std::uint16_t kEventNoSerializerId{0x8002U};

inline constexpr std::string_view kEventMissingInIpc{"ev_missing_in_ipc"};
inline constexpr std::uint16_t kEventMissingInIpcId{0x8003U};

inline constexpr flatbuffers::uoffset_t kFirstElementIndex{0U};
inline constexpr std::size_t kSingleElementCount{1U};

struct EventSpec {
    std::string_view name{};
    std::uint16_t event_id{0U};
    std::optional<std::uint32_t> null_serializer_max_size{};
};

struct BuiltConfig {
    std::shared_ptr<std::vector<std::uint8_t>> buffer{};
    std::shared_ptr<mw_someip_config::ServiceType const> service_type{};
    std::shared_ptr<mw_someip_config::ServiceInstance const> local_instance{};
    std::shared_ptr<mw_someip_config::ServiceInstance const> remote_instance{};
};

inline BuiltConfig BuildConfig(std::vector<EventSpec> const& event_specs) {
    flatbuffers::FlatBufferBuilder fbb{};

    std::vector<flatbuffers::Offset<mw_someip_config::Event>> fb_events{};
    fb_events.reserve(event_specs.size());
    for (auto const& spec : event_specs) {
        auto const name_offset = fbb.CreateString(spec.name.data(), spec.name.size());
        if (spec.null_serializer_max_size.has_value()) {
            auto const null_cfg =
                mw_someip_config::CreateNullSerializerConfig(fbb, *spec.null_serializer_max_size);
            fb_events.push_back(mw_someip_config::CreateEvent(
                fbb, spec.event_id, name_offset,
                mw_someip_config::SerializationConfig_NullSerializerConfig, null_cfg.Union()));
        } else {
            fb_events.push_back(mw_someip_config::CreateEvent(
                fbb, spec.event_id, name_offset, mw_someip_config::SerializationConfig_NONE,
                flatbuffers::Offset<void>{}));
        }
    }

    auto const local_inst = mw_someip_config::CreateServiceInstance(
        fbb, fbb.CreateString(kLocalSpecifier.data(), kLocalSpecifier.size()), kInstanceId);
    auto const local_instances_offset = fbb.CreateVector(&local_inst, kSingleElementCount);

    auto const remote_inst = mw_someip_config::CreateServiceInstance(
        fbb, fbb.CreateString(kRemoteSpecifier.data(), kRemoteSpecifier.size()), kInstanceId);
    auto const remote_instances_offset = fbb.CreateVector(&remote_inst, kSingleElementCount);

    auto const service_type_name =
        fbb.CreateString(kServiceTypeName.data(), kServiceTypeName.size());
    auto const events_offset = fbb.CreateVectorOfSortedTables<mw_someip_config::Event>(&fb_events);
    auto const service_type = mw_someip_config::CreateServiceType(
        fbb, service_type_name, kServiceId, kServiceMajorVersion, kServiceMinorVersion,
        events_offset, {}, local_instances_offset, remote_instances_offset);

    auto const service_types_offset = fbb.CreateVector(&service_type, kSingleElementCount);
    auto const root = mw_someip_config::CreateRoot(fbb, service_types_offset);
    fbb.Finish(root);

    auto const raw_buffer = std::make_shared<std::vector<std::uint8_t>>(
        fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());
    auto const* root_ptr = mw_someip_config::GetRoot(raw_buffer->data());
    auto const* st_ptr = root_ptr->service_types()->Get(kFirstElementIndex);
    auto const* local_ptr = st_ptr->local_service_instances()->Get(kFirstElementIndex);
    auto const* remote_ptr = st_ptr->remote_service_instances()->Get(kFirstElementIndex);

    return BuiltConfig{
        raw_buffer,
        std::shared_ptr<mw_someip_config::ServiceType const>{raw_buffer, st_ptr},
        std::shared_ptr<mw_someip_config::ServiceInstance const>{raw_buffer, local_ptr},
        std::shared_ptr<mw_someip_config::ServiceInstance const>{raw_buffer, remote_ptr},
    };
}

class ServiceInstanceTestFixture : public score::gateway_ipc_binding::testing::MwComTestFixture {
   public:
    static void SetUpTestSuite() {
        auto const serializer_cfg = BuildConfig({
            EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
            EventSpec{kEventNoSerializer, kEventNoSerializerId, std::nullopt},
        });
        config_path_ = ::testing::TempDir() + "service_instance_test_config.bin";
        {
            std::ofstream file{config_path_, std::ios::binary | std::ios::out};
            (void)std::copy(serializer_cfg.buffer->cbegin(), serializer_cfg.buffer->cend(),
                            std::ostreambuf_iterator<char>{file});
        }

        ASSERT_EQ(score_com_serializer_init(config_path_.c_str(), config_path_.size()),
                  score_com_serializer_result_ok);
        serializer_initialized_ = true;
    }

    static void TearDownTestSuite() {
        if (serializer_initialized_) {
            EXPECT_EQ(score_com_serializer_deinit(), score_com_serializer_result_ok);
            serializer_initialized_ = false;
        }
        if (!config_path_.empty()) {
            (void)std::remove(config_path_.c_str());
            config_path_.clear();
        }
    }

   protected:
    ServiceInstanceTestFixture() {
        ON_CALL(service_discovery_mock(), StopOfferService(::testing::_))
            .WillByDefault(::testing::Return(score::Result<void>{}));
    }

    using MwComTestFixture::StubInstanceResolved;

    void StubInstanceResolved(std::string_view const specifier,
                              std::initializer_list<std::string_view> const events) {
        MwComTestFixture::StubInstanceResolved(
            specifier, events,
            score::gateway_ipc_binding::testing::ServiceDeploymentSpec{
                kServiceTypeName, kServiceId, kServiceMajorVersion, kServiceMinorVersion,
                kInstanceId});
    }

    score::mw::com::impl::HandleType MakeHandle(
        std::string_view const specifier, std::initializer_list<std::string_view> const events) {
        return instance_holder().MakeHandle(
            specifier, events,
            score::gateway_ipc_binding::testing::ServiceDeploymentSpec{
                kServiceTypeName, kServiceId, kServiceMajorVersion, kServiceMinorVersion,
                kInstanceId});
    }

    BuiltConfig default_cfg_{BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    })};
    ::testing::StrictMock<socom::Runtime_mock> socom_runtime_{};

   private:
    static inline std::string config_path_{};
    static inline bool serializer_initialized_{false};
};

}  // namespace score::someip_gateway::gatewayd

#endif  // SCORE_GATEWAYD_TEST_SERVICE_INSTANCE_TEST_FIXTURE_H
