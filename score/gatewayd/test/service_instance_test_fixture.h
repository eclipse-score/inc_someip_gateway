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
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "flatbuffers/flatbuffers.h"
#include "score/config/mw_someip_config_generated.h"
#include "score/mw/com/impl/handle_type.h"
#include "score/serializer/serializer.h"
#include "score/test/mw_com_mocks.hpp"
#include "score/test/mw_com_test_fixture.hpp"

namespace score::someip_gateway::gatewayd {

inline constexpr std::string_view kServiceTypeName{"echo_service"};
inline constexpr std::uint16_t kServiceId{0x1234U};
inline constexpr std::uint8_t kServiceMajorVersion{1U};
inline constexpr std::uint32_t kServiceMinorVersion{0U};
inline constexpr std::string_view kLocalSpecifier{"gatewayd/local_echo"};
inline constexpr std::string_view kRemoteSpecifier{"gatewayd/remote_echo"};
inline constexpr std::uint16_t kInstanceId{1U};
inline constexpr std::uint16_t kDefaultGatewaydSampleSlots{20U};

inline constexpr std::string_view kEventSerialized{"ev_serialized"};
inline constexpr std::uint16_t kEventSerializedId{0x8001U};
inline constexpr std::uint32_t kMaxMessageSize{32U};

inline constexpr std::string_view kEventNoSerializer{"ev_no_serializer"};
inline constexpr std::uint16_t kEventNoSerializerId{0x8002U};

inline constexpr std::string_view kEventMissingInIpc{"ev_missing_in_ipc"};
inline constexpr std::uint16_t kEventMissingInIpcId{0x8003U};

inline constexpr std::string_view kEventSerialized2{"ev_serialized_2"};
inline constexpr std::uint16_t kEventSerialized2Id{0x8004U};
inline constexpr std::uint32_t kMaxMessageSize2{64U};

inline constexpr flatbuffers::uoffset_t kFirstElementIndex{0U};
inline constexpr std::size_t kSingleElementCount{1U};

struct EventSpec {
    std::string_view name{};
    std::uint16_t event_id{0U};
    std::optional<std::uint32_t> null_serializer_max_size{};
};

struct ServiceConfigParams {
    std::string_view service_type_name{kServiceTypeName};
    std::uint16_t service_id{kServiceId};
    std::uint8_t major_version{kServiceMajorVersion};
    std::uint32_t minor_version{kServiceMinorVersion};
    std::string_view local_specifier{kLocalSpecifier};
    std::string_view remote_specifier{kRemoteSpecifier};
    std::uint16_t instance_id{kInstanceId};
};

struct BuiltConfig {
    std::shared_ptr<std::vector<std::uint8_t>> buffer{};
    std::shared_ptr<mw_someip_config::ServiceType const> service_type{};
    std::shared_ptr<mw_someip_config::ServiceInstance const> local_instance{};
    std::shared_ptr<mw_someip_config::ServiceInstance const> remote_instance{};
};

inline BuiltConfig BuildConfig(std::vector<EventSpec> const& event_specs,
                               ServiceConfigParams const& params = {}) {
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
        fbb, fbb.CreateString(params.local_specifier.data(), params.local_specifier.size()),
        params.instance_id);
    auto const local_instances_offset = fbb.CreateVector(&local_inst, kSingleElementCount);

    auto const remote_inst = mw_someip_config::CreateServiceInstance(
        fbb, fbb.CreateString(params.remote_specifier.data(), params.remote_specifier.size()),
        params.instance_id);
    auto const remote_instances_offset = fbb.CreateVector(&remote_inst, kSingleElementCount);

    auto const service_type_name =
        fbb.CreateString(params.service_type_name.data(), params.service_type_name.size());
    // Preserve caller event order rather than sorting by event_id so positional socom::Event_id
    // scenarios with arbitrary event_id orderings are faithfully represented.
    auto const events_offset = fbb.CreateVector(fb_events);
    auto const service_type = mw_someip_config::CreateServiceType(
        fbb, service_type_name, params.service_id, params.major_version, params.minor_version,
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

class ScopedTempDir final {
   public:
    ScopedTempDir() {
        static std::atomic<std::uint64_t> counter{0U};
        auto const id = counter.fetch_add(1U, std::memory_order_relaxed);
        auto const dir_name = "gatewayd_ut_" + std::to_string(static_cast<long>(::getpid())) + "_" +
                              std::to_string(id);
        path_ = std::filesystem::temp_directory_path() / dir_name;
        std::error_code ec{};
        (void)std::filesystem::create_directories(path_, ec);
    }

    ~ScopedTempDir() noexcept {
        if (!path_.empty()) {
            std::error_code ec{};
            (void)std::filesystem::remove_all(path_, ec);
        }
    }

    ScopedTempDir(ScopedTempDir const&) = delete;
    ScopedTempDir& operator=(ScopedTempDir const&) = delete;
    ScopedTempDir(ScopedTempDir&& other) noexcept : path_{std::move(other.path_)} {
        other.path_.clear();
    }
    ScopedTempDir& operator=(ScopedTempDir&& other) noexcept {
        if (this != &other) {
            if (!path_.empty()) {
                std::error_code ec{};
                (void)std::filesystem::remove_all(path_, ec);
            }
            path_ = std::move(other.path_);
            other.path_.clear();
        }
        return *this;
    }

    std::filesystem::path const& path() const noexcept { return path_; }

   private:
    std::filesystem::path path_{};
};

class ServiceInstanceTestFixture : public score::testing::MwComTestFixture {
   protected:
    void SetUp() override {
        auto const serializer_cfg = BuildConfig({
            EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
            EventSpec{kEventNoSerializer, kEventNoSerializerId, std::nullopt},
            EventSpec{kEventSerialized2, kEventSerialized2Id, kMaxMessageSize2},
        });
        ReinitSerializer(serializer_cfg);
    }

    void TearDown() override { DeinitSerializer(); }

    void ReinitSerializer(BuiltConfig const& cfg) {
        ASSERT_NE(cfg.buffer, nullptr);
        DeinitSerializer();

        auto const file_path =
            temp_dir_.path() / ("serializer_config_" + std::to_string(reinit_index_++) + ".bin");
        auto const file_str = file_path.string();
        {
            std::ofstream file{file_path, std::ios::binary | std::ios::out | std::ios::trunc};
            ASSERT_TRUE(file.is_open());
            (void)std::copy(cfg.buffer->cbegin(), cfg.buffer->cend(),
                            std::ostreambuf_iterator<char>{file});
        }
        ASSERT_EQ(score_com_serializer_init(file_str.c_str(), file_str.size()),
                  score_com_serializer_result_ok);
        serializer_initialized_ = true;
    }

    void DeinitSerializer() {
        if (serializer_initialized_) {
            EXPECT_EQ(score_com_serializer_deinit(), score_com_serializer_result_ok);
            serializer_initialized_ = false;
        }
    }

    using MwComTestFixture::StubInstanceNotResolved;
    using MwComTestFixture::StubInstanceResolved;

    void StubInstanceResolved(std::string_view const specifier,
                              std::initializer_list<std::string_view> const events) {
        MwComTestFixture::StubInstanceResolved(
            specifier, events,
            score::testing::ServiceDeploymentSpec{kServiceTypeName, kServiceId,
                                                  kServiceMajorVersion, kServiceMinorVersion,
                                                  kInstanceId, kDefaultGatewaydSampleSlots});
    }

    score::mw::com::impl::HandleType MakeHandle(
        std::string_view const specifier, std::initializer_list<std::string_view> const events,
        std::uint16_t const instance_id = kInstanceId) {
        return instance_holder().MakeHandle(
            specifier, events,
            score::testing::ServiceDeploymentSpec{kServiceTypeName, kServiceId,
                                                  kServiceMajorVersion, kServiceMinorVersion,
                                                  instance_id, kDefaultGatewaydSampleSlots});
    }

    BuiltConfig default_cfg_{BuildConfig({
        EventSpec{kEventSerialized, kEventSerializedId, kMaxMessageSize},
    })};

   private:
    ScopedTempDir temp_dir_{};
    std::size_t reinit_index_{0U};
    bool serializer_initialized_{false};
};

}  // namespace score::someip_gateway::gatewayd

#endif  // SCORE_GATEWAYD_TEST_SERVICE_INSTANCE_TEST_FIXTURE_H
