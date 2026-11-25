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

#include "score/gateway_ipc_binding/impl/consumer_service_binding.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/gateway_ipc_binding/test/unit/ipc_binding_test_fixture.hpp"
#include "score/memory/data_type_size_info.h"
#include "score/mw/com/impl/bindings/mock_binding/sample_ptr.h"
#include "score/mw/com/impl/com_error.h"
#include "score/mw/com/impl/find_service_handle.h"
#include "score/mw/com/impl/find_service_handler.h"
#include "score/mw/com/impl/generic_proxy_event_binding.h"
#include "score/mw/com/impl/handle_type.h"
#include "score/mw/com/impl/instance_specifier.h"
#include "score/mw/com/impl/plumbing/sample_ptr.h"
#include "score/mw/com/impl/scoped_event_receive_handler.h"
#include "score/socom/error.hpp"
#include "score/socom/event.hpp"
#include "score/socom/payload.hpp"
#include "score/socom/server_connector.hpp"

namespace score::gateway_ipc_binding::mw_com {
namespace {

using ::testing::_;
using ::testing::An;
using ::testing::ElementsAreArray;
using ::testing::Return;

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "Pinned host-endian sample prefix vectors assume little-endian target");

constexpr std::size_t kZeroConfiguredPayloadSize{0U};
constexpr std::size_t kByteAlignment{1U};
constexpr std::size_t kConfiguredPayloadSize16{16U};
constexpr std::size_t kMinSomeipdPeerSampleSize{8U};
constexpr std::size_t kUndersizedSomeipdPeerSampleSize{7U};
constexpr std::size_t kMinPeerSampleSize24{24U};
constexpr std::size_t kUndersizedPeerSampleSize23{23U};
constexpr std::size_t kLargerPeerSampleSize40{40U};
constexpr std::size_t kAdoptedPayloadSize32{32U};
constexpr std::size_t kFourBytePayloadSize{4U};
constexpr std::size_t kMaxMinusOnePayloadSize15{15U};
constexpr std::size_t kSingleSlotSampleCount{1U};
constexpr std::size_t kCustomSampleCount{7U};
constexpr std::size_t kExpectedBridgedMethodCount{0U};
constexpr std::size_t kTwoConfiguredEvents{2U};
constexpr std::size_t kEvent0Index{0U};
constexpr std::size_t kEvent1Index{1U};
constexpr std::size_t kZeroDeliveredSamples{0U};
constexpr std::size_t kSingleDeliveredSampleCount{1U};
constexpr std::size_t kTwoDeliveredSampleCount{2U};
constexpr std::size_t kDroppedBatchSampleCount{4U};
constexpr std::size_t kExpectedForwardedSampleCount{4U};
constexpr std::size_t kReceiveNotificationBurstCount{6U};
constexpr std::size_t kExpectedMaxConcurrentGetNewSamples{1U};
constexpr std::size_t kConcurrentCollisionThreshold{2U};
constexpr std::chrono::milliseconds kContentionObservationWindow{20};
constexpr std::chrono::seconds kSynchronizationTimeout{5};
constexpr std::uint16_t kFirstPeerInstanceId{1U};
constexpr std::uint16_t kSecondPeerInstanceId{2U};
constexpr std::uint32_t kFindHandleUid{11U};
constexpr socom::Event_id kSocomEvent0{0U};
constexpr socom::Event_id kSocomEvent1{1U};
constexpr socom::Event_id kOutOfRangeEventId{99U};
constexpr socom::Method_id kDummyMethodId{0U};
constexpr std::byte kPaddingFillByte{0xCCU};
constexpr std::uint8_t kPayload15SeedByte{0x10U};
constexpr std::uint8_t kPayload16SeedByte{0x50U};
constexpr std::uint8_t kAdoptedPayloadSeedByte{0xA0U};

// Pinned 8-byte sample prefixes: bytes [0..3] = little-endian u32 length, bytes [4..7] = reserved.
constexpr std::array<std::byte, kSample_prefix_size> kPrefixLength0{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
};
constexpr std::array<std::byte, kSample_prefix_size> kPrefixLength4{
    std::byte{0x04U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
};
constexpr std::array<std::byte, kSample_prefix_size> kPrefixLength4WithNonZeroReserved{
    std::byte{0x04U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0xDEU}, std::byte{0xADU}, std::byte{0xBEU}, std::byte{0xEFU},
};
constexpr std::array<std::byte, kSample_prefix_size> kPrefixLength15{
    std::byte{0x0FU}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
};
constexpr std::array<std::byte, kSample_prefix_size> kPrefixLength16{
    std::byte{0x10U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
};
constexpr std::array<std::byte, kSample_prefix_size> kPrefixLength17{
    std::byte{0x11U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
};
constexpr std::array<std::byte, kSample_prefix_size> kPrefixLength32{
    std::byte{0x20U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
};
constexpr std::array<std::byte, kSample_prefix_size> kPrefixLength33{
    std::byte{0x21U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
};
constexpr std::array<std::byte, kSample_prefix_size> kPrefixLengthSignBitU32{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
};
constexpr std::array<std::byte, kSample_prefix_size> kPrefixLengthUint32Max{
    std::byte{0xFFU}, std::byte{0xFFU}, std::byte{0xFFU}, std::byte{0xFFU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
};

template <std::size_t N>
constexpr std::array<std::byte, N> MakeSequentialPayload(std::uint8_t const seed) noexcept {
    std::array<std::byte, N> payload{};
    for (std::size_t i = 0U; i < N; ++i) {
        payload[i] = static_cast<std::byte>(static_cast<std::uint8_t>(seed + i));
    }
    return payload;
}

constexpr std::array<std::byte, kFourBytePayloadSize> kSignBitBytes{
    std::byte{0x00U}, std::byte{0x7FU}, std::byte{0x80U}, std::byte{0xFFU}};
constexpr std::array<std::byte, kFourBytePayloadSize> kReconnectBytes{
    std::byte{0x11U}, std::byte{0x22U}, std::byte{0x33U}, std::byte{0x44U}};
constexpr auto kExpected15bPayload =
    MakeSequentialPayload<kMaxMinusOnePayloadSize15>(kPayload15SeedByte);
constexpr auto kExpected16bPayload =
    MakeSequentialPayload<kConfiguredPayloadSize16>(kPayload16SeedByte);
constexpr auto kExpected32bPayload =
    MakeSequentialPayload<kAdoptedPayloadSize32>(kAdoptedPayloadSeedByte);

template <typename CallbackT>
void DeliverSample(CallbackT& callback, score::mw::com::impl::SamplePtr<void> sample) {
    if constexpr (std::is_invocable_v<CallbackT&, score::mw::com::impl::SamplePtr<void>>) {
        callback(std::move(sample));
    } else {
        callback(std::move(sample), {});
    }
}

template <std::size_t BufferSize>
struct AlignedSampleBuffer {
    alignas(kSample_alignment) std::array<std::byte, BufferSize> bytes{};
};

template <std::size_t BufferSize>
AlignedSampleBuffer<BufferSize> MakeFramedSample(
    std::array<std::byte, kSample_prefix_size> const& prefix,
    score::cpp::span<std::byte const> const payload = {}) {
    AlignedSampleBuffer<BufferSize> sample{};
    std::fill(sample.bytes.begin(), sample.bytes.end(), kPaddingFillByte);
    std::copy(prefix.begin(), prefix.end(), sample.bytes.begin());
    std::copy(payload.begin(), payload.end(),
              sample.bytes.begin() + static_cast<std::ptrdiff_t>(kSample_prefix_size));
    return sample;
}

class ScopedSampleDeleterCounter {
   public:
    explicit ScopedSampleDeleterCounter(std::atomic<std::size_t>& counter) noexcept {
        active_counter_.store(&counter, std::memory_order_release);
    }
    ~ScopedSampleDeleterCounter() noexcept {
        active_counter_.store(nullptr, std::memory_order_release);
    }

    ScopedSampleDeleterCounter(ScopedSampleDeleterCounter const&) = delete;
    ScopedSampleDeleterCounter& operator=(ScopedSampleDeleterCounter const&) = delete;
    ScopedSampleDeleterCounter(ScopedSampleDeleterCounter&&) = delete;
    ScopedSampleDeleterCounter& operator=(ScopedSampleDeleterCounter&&) = delete;

    static void OnDelete(void*) noexcept {
        if (auto* const counter = active_counter_.load(std::memory_order_acquire);
            counter != nullptr) {
            (void)counter->fetch_add(1U, std::memory_order_acq_rel);
        }
    }

   private:
    inline static std::atomic<std::atomic<std::size_t>*> active_counter_{nullptr};
};

void UpdateAtomicMax(std::atomic<std::size_t>& target, std::size_t const candidate) noexcept {
    auto prev = target.load(std::memory_order_relaxed);
    while ((prev < candidate) &&
           !target.compare_exchange_weak(prev, candidate, std::memory_order_acq_rel,
                                         std::memory_order_relaxed)) {
    }
}

class ConsumerServiceBindingTest : public IpcBindingTestFixture {
   protected:
    using FindHandler = score::mw::com::impl::FindServiceHandler<score::mw::com::impl::HandleType>;
    using ReceiveHandlerWeakPtr = std::weak_ptr<score::mw::com::impl::ScopedEventReceiveHandler>;
    using MockProxyEvent = ::testing::NiceMock<score::testing::GenericProxyEvent>;
    using ProxySampleCallback = score::mw::com::impl::GenericProxyEventBinding::Callback;
    using SampleTrackerFactory = score::mw::com::impl::TrackerGuardFactory;

    struct CapturedEventUpdates {
        std::mutex mutex{};
        std::vector<std::vector<std::byte>> payloads{};
        std::vector<bool> has_sample_user_data{};
    };

    static score::mw::com::impl::InstanceSpecifier MakeExpectedSpecifier(
        std::string_view const specifier = kValidBridgedSpecifier) {
        return score::mw::com::impl::InstanceSpecifier::Create(std::string{specifier}).value();
    }

    static Service_config MakeTwoEventConsumerConfig(
        std::size_t const ev0_max_payload = kEvent0MaxPayload,
        std::size_t const ev1_max_payload = kEvent1MaxPayload,
        std::size_t const max_sample_count = kDefaultMaxSampleCount) {
        return MakeMultiEventServiceConfig(
            Role::consumer,
            {
                Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, ev0_max_payload},
                Event_config{std::string{kEvent1}, Mw_com_event_id{kEvent1Id}, ev1_max_payload},
            },
            kValidBridgedSpecifier, kDefaultServiceId, max_sample_count);
    }

    score::mw::com::impl::HandleType MakeSingleEventPeerHandle(
        std::uint16_t const instance_id = kDefaultInstanceId) {
        return MakeHandle(kValidBridgedSpecifier, {kEvent0}, kDefaultServiceId, instance_id);
    }

    MockProxyEvent* ExpectSizedProxyEvent(std::string_view const event_name,
                                          std::size_t const sample_size) {
        auto* const ev = ExpectProxyWithEvent(event_name);
        StubPeerEventSize(*ev, sample_size);
        return ev;
    }

    score::testing::MockProxyWithEvents ExpectTwoEventProxy(
        std::size_t const ev0_sample_size, std::size_t const ev1_sample_size,
        std::size_t const ev0_alignment = kSample_alignment,
        std::size_t const ev1_alignment = kSample_alignment) {
        auto mocks = ExpectProxyWithEvents({kEvent0, kEvent1});
        StubPeerEventSize(*mocks.events[kEvent0Index], ev0_sample_size, ev0_alignment);
        StubPeerEventSize(*mocks.events[kEvent1Index], ev1_sample_size, ev1_alignment);
        return mocks;
    }

    score::mw::com::impl::FindServiceHandle ExpectDiscoveryCapture(
        FindHandler& out_handler, std::string_view const specifier = kValidBridgedSpecifier,
        std::uint32_t const handle_uid = kFindHandleUid) {
        auto const expected_specifier = MakeExpectedSpecifier(specifier);
        auto const find_handle = score::mw::com::impl::make_FindServiceHandle(handle_uid);
        EXPECT_CALL(service_discovery_mock(), StartFindService(_, expected_specifier))
            .Times(1)
            .WillOnce([&out_handler, find_handle](FindHandler handler,
                                                  score::mw::com::impl::InstanceSpecifier) {
                out_handler = std::move(handler);
                return score::Result<score::mw::com::impl::FindServiceHandle>{find_handle};
            });
        EXPECT_CALL(service_discovery_mock(), StopFindService(find_handle))
            .Times(1)
            .WillOnce(Return(score::Result<void>{}));
        return find_handle;
    }

    static void StubPeerEventSize(MockProxyEvent& event_mock, std::size_t const sample_size,
                                  std::size_t const alignment = kSample_alignment) {
        ON_CALL(event_mock, GetDataTypeSizeInfo())
            .WillByDefault(Return(score::memory::DataTypeSizeInfo{sample_size, alignment}));
    }

    static void ExpectEventNeverSubscribed(MockProxyEvent& event_mock) {
        EXPECT_CALL(event_mock, Subscribe(_)).Times(0);
        EXPECT_CALL(event_mock, SetReceiveHandler(_)).Times(0);
        EXPECT_CALL(event_mock, UnsetReceiveHandler()).Times(0);
        EXPECT_CALL(event_mock, Unsubscribe()).Times(0);
    }

    static void ExpectEventSubscriptionLifecycle(
        MockProxyEvent& event_mock, std::size_t const max_sample_count = kDefaultMaxSampleCount,
        ReceiveHandlerWeakPtr* const out_receive_handler = nullptr,
        std::size_t* const out_subscribe_calls = nullptr,
        std::size_t* const out_unsubscribe_calls = nullptr) {
        ::testing::InSequence const subscription_seq{};
        EXPECT_CALL(event_mock, Subscribe(max_sample_count))
            .Times(1)
            .WillOnce([out_subscribe_calls](std::size_t) {
                if (out_subscribe_calls != nullptr) {
                    ++(*out_subscribe_calls);
                }
                return score::Result<void>{};
            });
        EXPECT_CALL(event_mock, SetReceiveHandler(_))
            .Times(1)
            .WillOnce([out_receive_handler](ReceiveHandlerWeakPtr handler) {
                if (out_receive_handler != nullptr) {
                    *out_receive_handler = std::move(handler);
                }
                return score::Result<void>{};
            });
        EXPECT_CALL(event_mock, UnsetReceiveHandler())
            .Times(1)
            .WillOnce(Return(score::Result<void>{}));
        EXPECT_CALL(event_mock, Unsubscribe())
            .Times(1)
            .WillOnce([out_unsubscribe_calls]() noexcept {
                if (out_unsubscribe_calls != nullptr) {
                    ++(*out_unsubscribe_calls);
                }
            });
    }

    static void ExpectPeerSessionWithSubscribedEvent(
        ::testing::NiceMock<socom::Server_connector_mock>& server_connector,
        MockProxyEvent& event_mock, ::testing::Sequence& lifecycle_seq,
        ReceiveHandlerWeakPtr* const out_receive_handler = nullptr,
        std::size_t* const out_subscribe_calls = nullptr,
        socom::Disabled_server_connector::Callbacks const* const disable_unsubscribe_callbacks =
            nullptr) {
        auto const in_disable = std::make_shared<std::atomic<bool>>(false);
        EXPECT_CALL(server_connector, enable())
            .InSequence(lifecycle_seq)
            .WillOnce(Return(&server_connector));
        EXPECT_CALL(event_mock, Subscribe(kDefaultMaxSampleCount))
            .InSequence(lifecycle_seq)
            .WillOnce([out_subscribe_calls](std::size_t) {
                if (out_subscribe_calls != nullptr) {
                    ++(*out_subscribe_calls);
                }
                return score::Result<void>{};
            });
        EXPECT_CALL(event_mock, SetReceiveHandler(_))
            .InSequence(lifecycle_seq)
            .WillOnce([out_receive_handler](ReceiveHandlerWeakPtr handler) {
                if (out_receive_handler != nullptr) {
                    *out_receive_handler = std::move(handler);
                }
                return score::Result<void>{};
            });
        EXPECT_CALL(event_mock, UnsetReceiveHandler())
            .InSequence(lifecycle_seq)
            .WillOnce(Return(score::Result<void>{}));
        EXPECT_CALL(server_connector, disable())
            .InSequence(lifecycle_seq)
            .WillOnce([&server_connector, disable_unsubscribe_callbacks, in_disable]() {
                in_disable->store(true, std::memory_order_release);
                if (disable_unsubscribe_callbacks != nullptr) {
                    disable_unsubscribe_callbacks->on_event_subscription_change(
                        server_connector, kSocomEvent0, socom::Event_state::unsubscribed);
                }
                in_disable->store(false, std::memory_order_release);
                return &server_connector;
            });
        EXPECT_CALL(event_mock, Unsubscribe())
            .InSequence(lifecycle_seq)
            .WillOnce([in_disable]() noexcept {
                EXPECT_FALSE(in_disable->load(std::memory_order_acquire));
            });
    }

    static void ExpectCapturedEventUpdates(
        ::testing::NiceMock<socom::Server_connector_mock>& connector,
        socom::Event_id const event_id, std::size_t const expected_count,
        CapturedEventUpdates& out_captured) {
        EXPECT_CALL(connector, update_event(event_id, _))
            .Times(expected_count)
            .WillRepeatedly([&out_captured](socom::Event_id, socom::Payload payload) {
                auto const* const sample = payload.get_user_data<score::mw::com::SamplePtr<void>>();
                auto const has_sample = (sample != nullptr) && (sample->get() != nullptr);
                auto const span = payload.data();
                std::lock_guard<std::mutex> const lock{out_captured.mutex};
                out_captured.has_sample_user_data.push_back(has_sample);
                out_captured.payloads.emplace_back(span.begin(), span.end());
                return score::Result<void>{};
            });
    }

    std::unique_ptr<Consumer_service_binding> StartAndSubscribeSingleEvent(
        socom::Disabled_server_connector::Callbacks const& server_callbacks,
        socom::Server_connector_mock& server_connector,
        Service_config const& config = MakeServiceConfig(Role::consumer)) {
        FindHandler find_handler{};
        auto const find_handle = ExpectDiscoveryCapture(find_handler, config.instance_specifier);
        auto binding_res = Consumer_service_binding::create(socom_runtime_, config);
        EXPECT_TRUE(binding_res.has_value());
        if (!binding_res.has_value()) {
            return nullptr;
        }
        EXPECT_NE(*binding_res, nullptr);
        if (*binding_res == nullptr) {
            return nullptr;
        }
        auto const start_res = (*binding_res)->start();
        EXPECT_TRUE(start_res.has_value());
        if (!start_res.has_value()) {
            return nullptr;
        }
        find_handler({MakeHandle(config.instance_specifier, {kEvent0}, config.service_id,
                                 config.instance_id)},
                     find_handle);
        server_callbacks.on_event_subscription_change(server_connector, kSocomEvent0,
                                                      socom::Event_state::subscribed);
        return std::move(*binding_res);
    }

    static auto DeliverSingleMockSample(std::byte* const sample_base) {
        return [sample_base](ProxySampleCallback&& cb, SampleTrackerFactory& tracker) {
            DeliverSample(cb, testing::MakeMockSample(sample_base, tracker));
            return score::Result<std::size_t>{kSingleDeliveredSampleCount};
        };
    }

    static auto DeliverDroppedSampleBatch(std::byte* const over_max_base,
                                          std::byte* const high_bit_base,
                                          std::byte* const uint32_max_base) {
        return [over_max_base, high_bit_base, uint32_max_base](ProxySampleCallback&& cb,
                                                               SampleTrackerFactory& tracker) {
            DeliverSample(cb, score::mw::com::impl::SamplePtr<void>{});
            DeliverSample(cb, testing::MakeMockSample(over_max_base, tracker));
            DeliverSample(cb, testing::MakeMockSample(high_bit_base, tracker));
            DeliverSample(cb, testing::MakeMockSample(uint32_max_base, tracker));
            return score::Result<std::size_t>{kDroppedBatchSampleCount};
        };
    }

    static score::mw::com::impl::SamplePtr<void> MakeTrackedMockSample(
        std::byte* const sample_base, SampleTrackerFactory& tracker) {
        auto guard = tracker.TakeGuard();
        if (!guard.has_value()) {
            return score::mw::com::impl::SamplePtr<void>{};
        }
        return score::mw::com::impl::SamplePtr<void>{
            score::mw::com::impl::mock_binding::SamplePtr{sample_base,
                                                          &ScopedSampleDeleterCounter::OnDelete},
            std::move(*guard)};
    }

    static auto DeliverTrackedMockSample(std::byte* const sample_base) {
        return [sample_base](ProxySampleCallback&& cb, SampleTrackerFactory& tracker) {
            auto tracked_sample = MakeTrackedMockSample(sample_base, tracker);
            if (tracked_sample.get() == nullptr) {
                return score::Result<std::size_t>{kZeroDeliveredSamples};
            }
            DeliverSample(cb, std::move(tracked_sample));
            return score::Result<std::size_t>{kSingleDeliveredSampleCount};
        };
    }
};

TEST_F(ConsumerServiceBindingTest, RejectsInvalidOrUndeployedSpecifierBeforeConnectorCreation) {
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .Times(0);
    StubInstanceNotResolved(kSecondBridgedSpecifier);

    auto const expected_error =
        MakeError(Mw_com_binding_error::logic_error_invalid_instance_specifier);

    auto const malformed_cfg = MakeServiceConfig(Role::consumer, kInvalidBridgedSpecifier);
    auto const malformed_res = Consumer_service_binding::create(socom_runtime_, malformed_cfg);
    ASSERT_FALSE(malformed_res.has_value());
    EXPECT_EQ(malformed_res.error(), expected_error);

    auto undeployed_cfg = MakeServiceConfig(Role::consumer, kSecondBridgedSpecifier);
    undeployed_cfg.service_id = kSecondServiceId;
    auto const undeployed_res = Consumer_service_binding::create(socom_runtime_, undeployed_cfg);
    ASSERT_FALSE(undeployed_res.has_value());
    EXPECT_EQ(undeployed_res.error(), expected_error);
}

TEST_F(ConsumerServiceBindingTest, PropagatesConnectorSetupFailureWithExactInterfaceDefinition) {
    StubInstanceResolved(kValidBridgedSpecifier, {kEvent0, kEvent1});
    auto const cfg = MakeTwoEventConsumerConfig();

    std::optional<socom::Server_service_interface_definition> captured_def{};
    std::optional<socom::Service_instance> captured_instance{};
    EXPECT_CALL(socom_runtime_, make_server_connector(_, _, _))
        .InSequence(server_connector_sequence())
        .WillOnce([&captured_def, &captured_instance](
                      socom::Server_service_interface_definition definition,
                      socom::Service_instance instance, socom::Disabled_server_connector::Callbacks)
                      -> score::Result<socom::Disabled_server_connector::Uptr> {
            captured_def.emplace(std::move(definition));
            captured_instance.emplace(std::move(instance));
            return score::MakeUnexpected(socom::Error::runtime_error_request_rejected);
        });
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .Times(0);

    auto const res = Consumer_service_binding::create(socom_runtime_, cfg);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), MakeError(Mw_com_binding_error::runtime_error_service_setup_failed));

    ASSERT_TRUE(captured_def.has_value());
    EXPECT_EQ(captured_def->get_interface(), cfg.interface);
    EXPECT_EQ(captured_def->get_num_methods(), kExpectedBridgedMethodCount);
    EXPECT_EQ(captured_def->get_num_events(), kTwoConfiguredEvents);
    ASSERT_TRUE(captured_instance.has_value());
    EXPECT_EQ(*captured_instance, cfg.instance);
}

TEST_F(ConsumerServiceBindingTest, HandlesUnsupportedServerConnectorCallbacksSafely) {
    socom::Disabled_server_connector::Callbacks callbacks{};
    auto* const server_connector = ExpectServerConnectorCreated(&callbacks);
    EXPECT_CALL(*server_connector, enable()).Times(0);
    EXPECT_CALL(*server_connector, update_event(_, _)).Times(0);
    EXPECT_CALL(*server_connector, update_requested_event(_, _)).Times(0);
    EXPECT_CALL(service_discovery_mock(),
                StartFindService(_, An<score::mw::com::impl::InstanceSpecifier>()))
        .Times(0);
    EXPECT_CALL(service_discovery_mock(), StopFindService(_)).Times(0);

    auto binding_res =
        Consumer_service_binding::create(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_TRUE(binding_res.has_value());
    ASSERT_NE(*binding_res, nullptr);

    auto method_invocation = callbacks.on_method_call(*server_connector, kDummyMethodId,
                                                      socom::empty_payload(), std::nullopt);
    EXPECT_EQ(method_invocation, nullptr);

    callbacks.on_event_update_request(*server_connector, kSocomEvent0);
    callbacks.on_event_subscription_change(*server_connector, kOutOfRangeEventId,
                                           socom::Event_state::subscribed);

    auto alloc_res = callbacks.on_method_call_payload_allocate(*server_connector, kDummyMethodId);
    ASSERT_TRUE(alloc_res.has_value());
    EXPECT_TRUE(alloc_res->wdata().empty());
    EXPECT_EQ(alloc_res->get_user_data<score::mw::com::SamplePtr<void>>(), nullptr);
}

TEST_F(ConsumerServiceBindingTest, ReturnsFindFailedWithoutLeakingHandleWhenDiscoveryFails) {
    auto* const server_connector = ExpectServerConnectorCreated();
    EXPECT_CALL(*server_connector, enable()).Times(0);
    EXPECT_CALL(service_discovery_mock(), StartFindService(_, MakeExpectedSpecifier()))
        .WillOnce(Return(
            score::MakeUnexpected(score::mw::com::impl::ComErrc::kFindServiceHandlerFailure)));
    EXPECT_CALL(service_discovery_mock(), StopFindService(_)).Times(0);

    auto binding_res =
        Consumer_service_binding::create(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_TRUE(binding_res.has_value());

    auto const start_res = (*binding_res)->start();
    ASSERT_FALSE(start_res.has_value());
    EXPECT_EQ(start_res.error(),
              MakeError(Mw_com_binding_error::runtime_error_service_find_failed));
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(ConsumerServiceBindingTest, DISABLED_StartsDiscoveryOnceAndGuardsAgainstDuplicateStart) {
    auto* const server_connector = ExpectServerConnectorCreated();
    EXPECT_CALL(*server_connector, enable()).Times(0);

    auto const expected_specifier = MakeExpectedSpecifier();
    auto const expected_find_handle = score::mw::com::impl::make_FindServiceHandle(kFindHandleUid);

    ON_CALL(service_discovery_mock(), StartFindService(_, expected_specifier))
        .WillByDefault(Return(
            score::MakeUnexpected(score::mw::com::impl::ComErrc::kFindServiceHandlerFailure)));
    EXPECT_CALL(service_discovery_mock(), StartFindService(_, expected_specifier))
        .Times(1)
        .WillOnce(Return(expected_find_handle));
    EXPECT_CALL(service_discovery_mock(), StopFindService(expected_find_handle))
        .Times(1)
        .WillOnce(Return(score::Result<void>{}));

    auto binding_res =
        Consumer_service_binding::create(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_TRUE(binding_res.has_value());
    ASSERT_TRUE((*binding_res)->start().has_value());

    auto const second_start = (*binding_res)->start();
    ASSERT_FALSE(second_start.has_value());
    EXPECT_EQ(second_start.error(), MakeError(Mw_com_binding_error::logic_error_already_started));
}

TEST_F(ConsumerServiceBindingTest, HandlesSynchronousFindNotificationInsideStartFindService) {
    auto* const server_connector = ExpectServerConnectorCreated();
    EXPECT_CALL(*server_connector, enable()).Times(1).WillOnce(Return(server_connector));
    EXPECT_CALL(*server_connector, disable()).Times(1).WillOnce(Return(server_connector));
    (void)ExpectSizedProxyEvent(kEvent0, kMinPeerSampleSize24);

    auto const expected_find_handle = score::mw::com::impl::make_FindServiceHandle(kFindHandleUid);
    auto const peer_handle = MakeHandle({kEvent0});
    EXPECT_CALL(service_discovery_mock(), StartFindService(_, MakeExpectedSpecifier()))
        .WillOnce([peer_handle, expected_find_handle](FindHandler handler,
                                                      score::mw::com::impl::InstanceSpecifier) {
            handler({peer_handle}, expected_find_handle);
            return score::Result<score::mw::com::impl::FindServiceHandle>{expected_find_handle};
        });
    EXPECT_CALL(service_discovery_mock(), StopFindService(expected_find_handle))
        .Times(1)
        .WillOnce(Return(score::Result<void>{}));

    auto binding_res =
        Consumer_service_binding::create(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_TRUE(binding_res.has_value());
    EXPECT_TRUE((*binding_res)->start().has_value());
}

TEST_F(ConsumerServiceBindingTest, EnforcesPeerSampleSizeBoundariesAndAdoptsLargerPeerPayloadSize) {
    StubInstanceResolved(kValidBridgedSpecifier, {kEvent0, kEvent1});
    auto const cfg =
        MakeTwoEventConsumerConfig(kConfiguredPayloadSize16, kZeroConfiguredPayloadSize);
    socom::Disabled_server_connector::Callbacks server_callbacks{};
    auto* const server_connector = ExpectServerConnectorCreated(&server_callbacks);

    // Attempts 1 & 2 (23 == B - 1 on ev0; 7 == B - 1 on ev1) rejected; 3 (exact B) & 4 (40) pass.
    (void)ExpectTwoEventProxy(kUndersizedPeerSampleSize23, kMinSomeipdPeerSampleSize,
                              kByteAlignment, kSample_alignment);
    (void)ExpectTwoEventProxy(kMinPeerSampleSize24, kUndersizedSomeipdPeerSampleSize,
                              kSample_alignment, kByteAlignment);
    (void)ExpectTwoEventProxy(kMinPeerSampleSize24, kMinSomeipdPeerSampleSize);
    auto attempt4 = ExpectTwoEventProxy(kLargerPeerSampleSize40, kLargerPeerSampleSize40);

    std::size_t enable_calls{0U};
    EXPECT_CALL(*server_connector, enable())
        .Times(2)
        .WillRepeatedly([&enable_calls, server_connector]() {
            ++enable_calls;
            return server_connector;
        });
    EXPECT_CALL(*server_connector, disable()).Times(2).WillRepeatedly(Return(server_connector));

    ReceiveHandlerWeakPtr ev0_receive_handler{};
    ExpectEventSubscriptionLifecycle(*attempt4.events[kEvent0Index], kDefaultMaxSampleCount,
                                     &ev0_receive_handler);

    auto valid_32b_sample =
        MakeFramedSample<kLargerPeerSampleSize40>(kPrefixLength32, kExpected32bPayload);
    auto oversized_33b_sample =
        MakeFramedSample<kLargerPeerSampleSize40>(kPrefixLength33, kExpected32bPayload);
    EXPECT_CALL(*attempt4.events[kEvent0Index], GetNewSamples(_, _))
        .WillOnce(DeliverSingleMockSample(valid_32b_sample.bytes.data()))
        .WillOnce(DeliverSingleMockSample(oversized_33b_sample.bytes.data()));

    CapturedEventUpdates captured{};
    ExpectCapturedEventUpdates(*server_connector, kSocomEvent0, kSingleDeliveredSampleCount,
                               captured);
    FindHandler find_handler{};
    auto const find_handle = ExpectDiscoveryCapture(find_handler);

    auto binding_res = Consumer_service_binding::create(socom_runtime_, cfg);
    ASSERT_TRUE(binding_res.has_value());
    ASSERT_TRUE((*binding_res)->start().has_value());

    auto const peer_handle = MakeHandle(kValidBridgedSpecifier, {kEvent0, kEvent1});
    find_handler({peer_handle}, find_handle);
    EXPECT_EQ(enable_calls, 0U);
    find_handler({peer_handle}, find_handle);
    EXPECT_EQ(enable_calls, 0U);
    find_handler({peer_handle}, find_handle);
    EXPECT_EQ(enable_calls, 1U);
    find_handler({}, find_handle);
    find_handler({peer_handle}, find_handle);
    EXPECT_EQ(enable_calls, 2U);

    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent0,
                                                  socom::Event_state::subscribed);
    auto locked_handler = ev0_receive_handler.lock();
    ASSERT_NE(locked_handler, nullptr);
    (*locked_handler)();
    (*locked_handler)();  // 33-byte sample (adopted 32 + 1) is dropped.
    ASSERT_EQ(captured.payloads.size(), kSingleDeliveredSampleCount);
    EXPECT_THAT(captured.payloads.front(), ElementsAreArray(kExpected32bPayload));
}

TEST_F(ConsumerServiceBindingTest, RecoversAfterProxyCreationOrMissingPeerEventFailure) {
    StubInstanceResolved(kValidBridgedSpecifier, {kEvent0, kEvent1});
    auto const cfg = MakeTwoEventConsumerConfig();

    auto* const server_connector = ExpectServerConnectorCreated();
    std::size_t enable_calls{0U};
    EXPECT_CALL(*server_connector, enable()).Times(1).WillOnce([&enable_calls, server_connector]() {
        ++enable_calls;
        return server_connector;
    });
    EXPECT_CALL(*server_connector, disable()).Times(1).WillOnce(Return(server_connector));

    ExpectProxyCreationFailure();

    auto partial_mocks = ExpectProxyWithEvents({kEvent0});
    EXPECT_CALL(*partial_mocks.proxy, IsEventProvided(kEvent1)).WillOnce(Return(false));
    StubPeerEventSize(*partial_mocks.events[kEvent0Index], kMinPeerSampleSize24);

    (void)ExpectTwoEventProxy(kMinPeerSampleSize24, kLargerPeerSampleSize40);

    FindHandler find_handler{};
    auto const find_handle = ExpectDiscoveryCapture(find_handler);

    auto binding_res = Consumer_service_binding::create(socom_runtime_, cfg);
    ASSERT_TRUE(binding_res.has_value());
    ASSERT_TRUE((*binding_res)->start().has_value());

    auto const peer_handle = MakeHandle(kValidBridgedSpecifier, {kEvent0, kEvent1});
    find_handler({peer_handle}, find_handle);
    EXPECT_EQ(enable_calls, 0U);
    find_handler({peer_handle}, find_handle);
    EXPECT_EQ(enable_calls, 0U);
    find_handler({peer_handle}, find_handle);
    EXPECT_EQ(enable_calls, 1U);
}

TEST_F(ConsumerServiceBindingTest,
       ReconcilesMultiEventSubscriptionsAcrossSynchronousEnableAndUnsubscribe) {
    StubInstanceResolved(kValidBridgedSpecifier, {kEvent0, kEvent1});
    auto const cfg =
        MakeTwoEventConsumerConfig(kEvent0MaxPayload, kEvent1MaxPayload, kCustomSampleCount);

    socom::Disabled_server_connector::Callbacks server_callbacks{};
    auto* const server_connector = ExpectServerConnectorCreated(&server_callbacks);
    std::size_t ev1_subscribe_calls{0U};
    std::size_t ev1_unsubscribe_calls{0U};
    EXPECT_CALL(*server_connector, enable())
        .Times(1)
        .WillOnce([&server_callbacks, server_connector, &ev1_subscribe_calls]() {
            server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent1,
                                                          socom::Event_state::subscribed);
            EXPECT_EQ(ev1_subscribe_calls, 0U);
            return server_connector;
        });
    EXPECT_CALL(*server_connector, disable()).Times(1).WillOnce(Return(server_connector));

    auto proxy_mocks = ExpectTwoEventProxy(kMinPeerSampleSize24, kLargerPeerSampleSize40);
    ExpectEventNeverSubscribed(*proxy_mocks.events[kEvent0Index]);

    ExpectEventSubscriptionLifecycle(*proxy_mocks.events[kEvent1Index], kCustomSampleCount, nullptr,
                                     &ev1_subscribe_calls, &ev1_unsubscribe_calls);

    FindHandler find_handler{};
    auto const find_handle = ExpectDiscoveryCapture(find_handler);

    auto binding_res = Consumer_service_binding::create(socom_runtime_, cfg);
    ASSERT_TRUE(binding_res.has_value());
    ASSERT_TRUE((*binding_res)->start().has_value());

    find_handler({MakeHandle(kValidBridgedSpecifier, {kEvent0, kEvent1})}, find_handle);
    EXPECT_EQ(ev1_subscribe_calls, 1U);
    EXPECT_EQ(ev1_unsubscribe_calls, 0U);

    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent1,
                                                  socom::Event_state::subscribed);
    EXPECT_EQ(ev1_subscribe_calls, 1U);

    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent1,
                                                  socom::Event_state::unsubscribed);
    EXPECT_EQ(ev1_unsubscribe_calls, 1U);
}

TEST_F(ConsumerServiceBindingTest, RollsBackSubscriptionWhenSetReceiveHandlerFails) {
    auto cfg = MakeServiceConfig(Role::consumer);
    cfg.max_sample_count = kCustomSampleCount;

    socom::Disabled_server_connector::Callbacks server_callbacks{};
    auto* const server_connector = ExpectServerConnectorCreated(&server_callbacks);
    EXPECT_CALL(*server_connector, enable()).Times(1).WillOnce(Return(server_connector));
    EXPECT_CALL(*server_connector, disable()).Times(1).WillOnce(Return(server_connector));

    auto* const ev0 = ExpectSizedProxyEvent(kEvent0, kMinPeerSampleSize24);

    std::size_t unsubscribe_calls{0U};
    EXPECT_CALL(*ev0, Subscribe(kCustomSampleCount))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)))
        .WillOnce(Return(score::Result<void>{}))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*ev0, SetReceiveHandler(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*ev0, Unsubscribe()).Times(2).WillRepeatedly([&unsubscribe_calls]() noexcept {
        ++unsubscribe_calls;
    });
    EXPECT_CALL(*ev0, UnsetReceiveHandler()).Times(1).WillOnce(Return(score::Result<void>{}));

    FindHandler find_handler{};
    auto const find_handle = ExpectDiscoveryCapture(find_handler);

    auto binding_res = Consumer_service_binding::create(socom_runtime_, cfg);
    ASSERT_TRUE(binding_res.has_value());
    ASSERT_TRUE((*binding_res)->start().has_value());
    find_handler({MakeHandle({kEvent0})}, find_handle);

    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent0,
                                                  socom::Event_state::subscribed);
    EXPECT_EQ(unsubscribe_calls, 0U);

    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent0,
                                                  socom::Event_state::unsubscribed);
    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent0,
                                                  socom::Event_state::subscribed);
    EXPECT_EQ(unsubscribe_calls, 1U);

    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent0,
                                                  socom::Event_state::unsubscribed);
    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent0,
                                                  socom::Event_state::subscribed);
    EXPECT_EQ(unsubscribe_calls, 1U);
}

TEST_F(ConsumerServiceBindingTest, ForwardsExactPayloadBytesAndDropsOutOfBoundsOrNullSamples) {
    StubInstanceResolved(kValidBridgedSpecifier, {kEvent0, kEvent1});
    auto const cfg = MakeTwoEventConsumerConfig(kEvent0MaxPayload, kConfiguredPayloadSize16);
    socom::Disabled_server_connector::Callbacks server_callbacks{};
    auto* const server_connector = ExpectServerConnectorCreated(&server_callbacks);
    EXPECT_CALL(*server_connector, enable()).Times(1).WillOnce(Return(server_connector));
    EXPECT_CALL(*server_connector, disable()).Times(1).WillOnce(Return(server_connector));

    auto proxy_mocks = ExpectTwoEventProxy(kMinPeerSampleSize24, kMinPeerSampleSize24);
    ReceiveHandlerWeakPtr ev1_receive_handler{};
    ExpectEventSubscriptionLifecycle(*proxy_mocks.events[kEvent1Index], kDefaultMaxSampleCount,
                                     &ev1_receive_handler);

    auto sign_bit_sample =
        MakeFramedSample<kMinPeerSampleSize24>(kPrefixLength4WithNonZeroReserved, kSignBitBytes);
    auto zero_length_sample = MakeFramedSample<kMinPeerSampleSize24>(kPrefixLength0);
    auto max_minus_one_sample =
        MakeFramedSample<kMinPeerSampleSize24>(kPrefixLength15, kExpected15bPayload);
    auto exact_max_sample =
        MakeFramedSample<kMinPeerSampleSize24>(kPrefixLength16, kExpected16bPayload);
    auto over_max_sample =
        MakeFramedSample<kMinPeerSampleSize24>(kPrefixLength17, kExpected16bPayload);
    auto high_bit_len_sample =
        MakeFramedSample<kMinPeerSampleSize24>(kPrefixLengthSignBitU32, kExpected16bPayload);
    auto uint32_max_len_sample =
        MakeFramedSample<kMinPeerSampleSize24>(kPrefixLengthUint32Max, kExpected16bPayload);

    EXPECT_CALL(*proxy_mocks.events[kEvent1Index], GetNewSamples(_, _))
        .WillOnce(DeliverSingleMockSample(sign_bit_sample.bytes.data()))
        .WillOnce(DeliverSingleMockSample(zero_length_sample.bytes.data()))
        .WillOnce(DeliverSingleMockSample(max_minus_one_sample.bytes.data()))
        .WillOnce(DeliverSingleMockSample(exact_max_sample.bytes.data()))
        .WillOnce(DeliverDroppedSampleBatch(over_max_sample.bytes.data(),
                                            high_bit_len_sample.bytes.data(),
                                            uint32_max_len_sample.bytes.data()))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));

    EXPECT_CALL(*server_connector, update_event(kSocomEvent0, _)).Times(0);
    CapturedEventUpdates captured{};
    ExpectCapturedEventUpdates(*server_connector, kSocomEvent1, kExpectedForwardedSampleCount,
                               captured);
    FindHandler find_handler{};
    auto const find_handle = ExpectDiscoveryCapture(find_handler);

    auto binding_res = Consumer_service_binding::create(socom_runtime_, cfg);
    ASSERT_TRUE(binding_res.has_value());
    ASSERT_TRUE((*binding_res)->start().has_value());
    find_handler({MakeHandle(kValidBridgedSpecifier, {kEvent0, kEvent1})}, find_handle);
    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent1,
                                                  socom::Event_state::subscribed);
    auto locked_handler = ev1_receive_handler.lock();
    ASSERT_NE(locked_handler, nullptr);
    for (std::size_t call_idx = 0U; call_idx < kReceiveNotificationBurstCount; ++call_idx) {
        (*locked_handler)();
    }

    ASSERT_EQ(captured.payloads.size(), kExpectedForwardedSampleCount);
    ASSERT_EQ(captured.has_sample_user_data.size(), kExpectedForwardedSampleCount);
    EXPECT_THAT(captured.has_sample_user_data, ::testing::Each(true));
    EXPECT_THAT(captured.payloads[0], ElementsAreArray(kSignBitBytes));
    EXPECT_TRUE(captured.payloads[1].empty());
    EXPECT_THAT(captured.payloads[2], ElementsAreArray(kExpected15bPayload));
    EXPECT_THAT(captured.payloads[3], ElementsAreArray(kExpected16bPayload));
}

TEST_F(ConsumerServiceBindingTest, RetainsZeroCopySampleGuardUntilSocomPayloadIsDestroyed) {
    std::atomic<std::size_t> sample_deleter_calls{0U};
    ScopedSampleDeleterCounter const deleter_scope{sample_deleter_calls};

    auto cfg = MakeServiceConfig(Role::consumer);
    cfg.max_sample_count = kSingleSlotSampleCount;

    socom::Disabled_server_connector::Callbacks server_callbacks{};
    auto* const server_connector = ExpectServerConnectorCreated(&server_callbacks);
    EXPECT_CALL(*server_connector, enable()).Times(1).WillOnce(Return(server_connector));
    EXPECT_CALL(*server_connector, disable()).Times(1).WillOnce(Return(server_connector));

    auto* const ev0 = ExpectSizedProxyEvent(kEvent0, kMinPeerSampleSize24);
    ReceiveHandlerWeakPtr ev0_receive_handler{};
    ExpectEventSubscriptionLifecycle(*ev0, kSingleSlotSampleCount, &ev0_receive_handler);

    auto raw_sample = MakeFramedSample<kMinPeerSampleSize24>(kPrefixLength4, kSignBitBytes);
    ON_CALL(*ev0, GetNewSamples(_, _))
        .WillByDefault(DeliverTrackedMockSample(raw_sample.bytes.data()));

    std::optional<socom::Payload> held_payload{};
    std::size_t forwarded_count{0U};
    EXPECT_CALL(*server_connector, update_event(kSocomEvent0, _))
        .Times(2)
        .WillRepeatedly([&held_payload, &forwarded_count](socom::Event_id, socom::Payload payload) {
            ++forwarded_count;
            held_payload.emplace(std::move(payload));
            return score::Result<void>{};
        });

    auto const binding = StartAndSubscribeSingleEvent(server_callbacks, *server_connector, cfg);
    ASSERT_NE(binding, nullptr);

    auto locked_handler = ev0_receive_handler.lock();
    ASSERT_NE(locked_handler, nullptr);

    (*locked_handler)();
    ASSERT_EQ(forwarded_count, 1U);
    ASSERT_TRUE(held_payload.has_value());
    auto const* const stored_sample =
        held_payload->get_user_data<score::mw::com::SamplePtr<void>>();
    ASSERT_NE(stored_sample, nullptr);
    EXPECT_EQ(stored_sample->get(), raw_sample.bytes.data());
    EXPECT_EQ(held_payload->data().data(), raw_sample.bytes.data() + kSample_prefix_size);
    ASSERT_EQ(held_payload->data().size(), kFourBytePayloadSize);
    EXPECT_THAT(held_payload->data(), ElementsAreArray(kSignBitBytes));
    EXPECT_EQ(sample_deleter_calls.load(std::memory_order_acquire), 0U);

    // Single SampleReferenceGuard is still held -> second slot acquisition yields 0 samples.
    (*locked_handler)();
    EXPECT_EQ(forwarded_count, 1U);
    EXPECT_EQ(sample_deleter_calls.load(std::memory_order_acquire), 0U);

    held_payload.reset();
    EXPECT_EQ(sample_deleter_calls.load(std::memory_order_acquire), 1U);

    (*locked_handler)();
    EXPECT_EQ(forwarded_count, 2U);
    EXPECT_EQ(sample_deleter_calls.load(std::memory_order_acquire), 1U);
    held_payload.reset();
    EXPECT_EQ(sample_deleter_calls.load(std::memory_order_acquire), 2U);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(ConsumerServiceBindingTest,
       DISABLED_DetachesBeforeUnsubscribingOnEmptyHandlesAndReconnectsWithResetSubscriptions) {
    socom::Disabled_server_connector::Callbacks server_callbacks{};
    auto* const server_connector = ExpectServerConnectorCreated(&server_callbacks);
    auto* const ev0_first = ExpectSizedProxyEvent(kEvent0, kMinPeerSampleSize24);
    auto* const ev0_second = ExpectSizedProxyEvent(kEvent0, kMinPeerSampleSize24);

    ::testing::Sequence connector_lifecycle_seq{};
    ExpectPeerSessionWithSubscribedEvent(*server_connector, *ev0_first, connector_lifecycle_seq,
                                         nullptr, nullptr, &server_callbacks);
    std::size_t ev0_second_subscribe_calls{0U};
    ReceiveHandlerWeakPtr ev0_second_handler{};
    ExpectPeerSessionWithSubscribedEvent(*server_connector, *ev0_second, connector_lifecycle_seq,
                                         &ev0_second_handler, &ev0_second_subscribe_calls);

    auto reconnect_sample = MakeFramedSample<kMinPeerSampleSize24>(kPrefixLength4, kReconnectBytes);
    EXPECT_CALL(*ev0_second, GetNewSamples(_, _))
        .Times(1)
        .WillOnce(DeliverSingleMockSample(reconnect_sample.bytes.data()));
    EXPECT_CALL(*server_connector, update_event(kSocomEvent0, _))
        .Times(1)
        .WillOnce([&server_callbacks, server_connector](socom::Event_id, socom::Payload payload) {
            EXPECT_THAT(payload.data(), ElementsAreArray(kReconnectBytes));
            server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent0,
                                                          socom::Event_state::subscribed);
            return score::Result<void>{};
        });

    FindHandler find_handler{};
    auto const find_handle = ExpectDiscoveryCapture(find_handler);
    auto binding_res =
        Consumer_service_binding::create(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_TRUE(binding_res.has_value());
    ASSERT_TRUE((*binding_res)->start().has_value());

    auto const handle2 = MakeSingleEventPeerHandle(kSecondPeerInstanceId);
    find_handler({MakeSingleEventPeerHandle(kFirstPeerInstanceId), handle2}, find_handle);
    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent0,
                                                  socom::Event_state::subscribed);
    find_handler({}, find_handle);
    find_handler({}, find_handle);
    find_handler({handle2}, find_handle);
    EXPECT_EQ(ev0_second_subscribe_calls, 0U);
    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent0,
                                                  socom::Event_state::subscribed);
    EXPECT_EQ(ev0_second_subscribe_calls, 1U);

    auto locked_handler = ev0_second_handler.lock();
    ASSERT_NE(locked_handler, nullptr);
    (*locked_handler)();
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(ConsumerServiceBindingTest,
       DISABLED_IgnoresDuplicateHandleAndReplacesDistinctHandleWhileAttached) {
    socom::Disabled_server_connector::Callbacks server_callbacks{};
    auto* const server_connector = ExpectServerConnectorCreated(&server_callbacks);
    auto* const ev0_first = ExpectSizedProxyEvent(kEvent0, kMinPeerSampleSize24);
    auto* const ev0_replacement = ExpectSizedProxyEvent(kEvent0, kMinPeerSampleSize24);

    ::testing::Sequence connector_lifecycle_seq{};
    ExpectPeerSessionWithSubscribedEvent(*server_connector, *ev0_first, connector_lifecycle_seq);
    std::size_t replacement_subscribe_calls{0U};
    ExpectPeerSessionWithSubscribedEvent(*server_connector, *ev0_replacement,
                                         connector_lifecycle_seq, nullptr,
                                         &replacement_subscribe_calls);

    FindHandler find_handler{};
    auto const find_handle = ExpectDiscoveryCapture(find_handler);

    auto binding_res =
        Consumer_service_binding::create(socom_runtime_, MakeServiceConfig(Role::consumer));
    ASSERT_TRUE(binding_res.has_value());
    ASSERT_TRUE((*binding_res)->start().has_value());

    auto const handle1 = MakeSingleEventPeerHandle(kFirstPeerInstanceId);
    auto const handle1_duplicate = handle1;
    auto const handle2 = MakeSingleEventPeerHandle(kSecondPeerInstanceId);
    ASSERT_TRUE(handle1_duplicate == handle1);
    ASSERT_FALSE(handle2 == handle1);

    find_handler({handle1}, find_handle);
    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent0,
                                                  socom::Event_state::subscribed);
    find_handler({handle1_duplicate}, find_handle);

    find_handler({handle2}, find_handle);
    EXPECT_EQ(replacement_subscribe_calls, 0U);
    server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent0,
                                                  socom::Event_state::subscribed);
    EXPECT_EQ(replacement_subscribe_calls, 1U);
}

// mw::com can invoke an event's ScopedEventReceiveHandler concurrently across threads.
// Serializing concurrent notifications prevents GetNewSamples() calls from racing or dropping
// reads.
TEST_F(ConsumerServiceBindingTest, SerializesConcurrentGetNewSamplesCallsForSameEvent) {
    socom::Disabled_server_connector::Callbacks server_callbacks{};
    auto* const server_connector = ExpectServerConnectorCreated(&server_callbacks);
    EXPECT_CALL(*server_connector, enable()).Times(1).WillOnce(Return(server_connector));
    EXPECT_CALL(*server_connector, disable()).Times(1).WillOnce(Return(server_connector));
    auto* const ev0 = ExpectSizedProxyEvent(kEvent0, kMinPeerSampleSize24);
    ReceiveHandlerWeakPtr ev0_receive_handler{};
    ExpectEventSubscriptionLifecycle(*ev0, kDefaultMaxSampleCount, &ev0_receive_handler);

    // Distinct payloads prove Sample 2 runs after Sample 1 instead of dropping or reordering.
    auto first_sample = MakeFramedSample<kMinPeerSampleSize24>(kPrefixLength4, kSignBitBytes);
    auto second_sample = MakeFramedSample<kMinPeerSampleSize24>(kPrefixLength4, kReconnectBytes);

    PromiseGate first_call_entered{};
    PromiseGate release_first_call{};
    PromiseGate second_thread_invoking{};
    PromiseGate overlap_or_early_exit{};
    std::atomic<bool> second_call_completed{false};
    std::atomic<std::size_t> active_get_new_samples{0U};
    std::atomic<std::size_t> max_concurrent_get_new_samples{0U};
    std::atomic<std::size_t> get_new_samples_invocations{0U};

    auto const make_get_new_samples_action =
        [&first_call_entered, &release_first_call, &overlap_or_early_exit, &active_get_new_samples,
         &max_concurrent_get_new_samples, &get_new_samples_invocations](
            AlignedSampleBuffer<kMinPeerSampleSize24>& sample, bool const hold_until_released) {
            return [&sample, hold_until_released, &first_call_entered, &release_first_call,
                    &overlap_or_early_exit, &active_get_new_samples,
                    &max_concurrent_get_new_samples, &get_new_samples_invocations](
                       ProxySampleCallback&& cb, SampleTrackerFactory& tracker) {
                (void)get_new_samples_invocations.fetch_add(1U, std::memory_order_acq_rel);
                auto const active =
                    active_get_new_samples.fetch_add(1U, std::memory_order_acq_rel) + 1U;
                UpdateAtomicMax(max_concurrent_get_new_samples, active);
                if (active >= kConcurrentCollisionThreshold) {
                    overlap_or_early_exit.Signal();
                }
                if (hold_until_released) {
                    first_call_entered.Signal();
                    EXPECT_TRUE(release_first_call.WaitFor(kSynchronizationTimeout));
                }
                DeliverSample(cb, testing::MakeMockSample(sample.bytes.data(), tracker));
                (void)active_get_new_samples.fetch_sub(1U, std::memory_order_acq_rel);
                return score::Result<std::size_t>{kSingleDeliveredSampleCount};
            };
        };
    EXPECT_CALL(*ev0, GetNewSamples(_, _))
        .Times(kTwoDeliveredSampleCount)
        .WillOnce(make_get_new_samples_action(first_sample, /*hold_until_released=*/true))
        .WillOnce(make_get_new_samples_action(second_sample, /*hold_until_released=*/false));

    CapturedEventUpdates captured{};
    ExpectCapturedEventUpdates(*server_connector, kSocomEvent0, kTwoDeliveredSampleCount, captured);
    auto const binding = StartAndSubscribeSingleEvent(server_callbacks, *server_connector);
    ASSERT_NE(binding, nullptr);
    auto const locked_handler = ev0_receive_handler.lock();
    ASSERT_NE(locked_handler, nullptr);

    // Unblock Worker 1 before joining threads if an early ASSERT_* aborts the test, and park
    // Worker 1 inside GetNewSamples() so Worker 2 contends with an in-flight sample read.
    ScopedThread first_worker{std::thread{[&locked_handler]() { (*locked_handler)(); }}};
    ScopedThread second_worker{};
    ScopedGateSignaler const unblock_first_on_exit{release_first_call};
    ASSERT_TRUE(first_call_entered.WaitFor(kSynchronizationTimeout));

    second_worker = ScopedThread{std::thread{[&locked_handler, &second_thread_invoking,
                                              &second_call_completed, &overlap_or_early_exit]() {
        second_thread_invoking.Signal();
        (*locked_handler)();
        second_call_completed.store(true, std::memory_order_release);
        overlap_or_early_exit.Signal();
    }}};
    ASSERT_TRUE(second_thread_invoking.WaitFor(kSynchronizationTimeout));

    // Fails immediately if Worker 2 enters GetNewSamples() concurrently or returns early before
    // Worker 1 is released.
    EXPECT_FALSE(overlap_or_early_exit.WaitFor(kContentionObservationWindow));
    EXPECT_FALSE(second_call_completed.load(std::memory_order_acquire));

    release_first_call.Signal();
    second_worker.Join();
    first_worker.Join();

    EXPECT_EQ(max_concurrent_get_new_samples.load(std::memory_order_acquire),
              kExpectedMaxConcurrentGetNewSamples);
    EXPECT_EQ(get_new_samples_invocations.load(std::memory_order_acquire),
              kTwoDeliveredSampleCount);
    ASSERT_EQ(captured.payloads.size(), kTwoDeliveredSampleCount);
    ASSERT_EQ(captured.has_sample_user_data.size(), kTwoDeliveredSampleCount);
    EXPECT_THAT(captured.has_sample_user_data, ::testing::Each(true));
    EXPECT_THAT(captured.payloads[0], ElementsAreArray(kSignBitBytes));
    EXPECT_THAT(captured.payloads[1], ElementsAreArray(kReconnectBytes));
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST_F(
    ConsumerServiceBindingTest,
    DISABLED_DrainsGetNewSamplesBeforeForwardingAndDefersReentrantUnsubscribeUntilSamplesRelease) {
    std::atomic<std::size_t> sample_deleter_calls{0U};
    ScopedSampleDeleterCounter const deleter_scope{sample_deleter_calls};

    socom::Disabled_server_connector::Callbacks server_callbacks{};
    auto* const server_connector = ExpectServerConnectorCreated(&server_callbacks);
    EXPECT_CALL(*server_connector, enable()).Times(1).WillOnce(Return(server_connector));
    EXPECT_CALL(*server_connector, disable()).Times(1).WillOnce(Return(server_connector));

    auto* const ev0 = ExpectSizedProxyEvent(kEvent0, kMinPeerSampleSize24);
    ReceiveHandlerWeakPtr ev0_receive_handler{};

    EXPECT_CALL(*ev0, Subscribe(kDefaultMaxSampleCount))
        .Times(1)
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*ev0, SetReceiveHandler(_))
        .Times(1)
        .WillOnce([&ev0_receive_handler](ReceiveHandlerWeakPtr handler) {
            ev0_receive_handler = std::move(handler);
            return score::Result<void>{};
        });

    std::mutex sync_mutex{};
    std::condition_variable sync_cv{};
    bool in_get_new_samples{false};
    bool get_new_samples_returned{false};
    bool in_update_event{false};
    bool unsubscribe_completed{false};
    std::size_t unset_calls{0U};
    std::size_t unsubscribe_calls{0U};
    ::testing::Sequence unsubscribe_seq{};
    EXPECT_CALL(*ev0, UnsetReceiveHandler())
        .Times(1)
        .InSequence(unsubscribe_seq)
        .WillOnce([&sync_mutex, &in_get_new_samples, &in_update_event, &sample_deleter_calls,
                   &unset_calls]() {
            std::lock_guard<std::mutex> const lock{sync_mutex};
            EXPECT_FALSE(in_get_new_samples);
            EXPECT_FALSE(in_update_event);
            EXPECT_EQ(sample_deleter_calls.load(std::memory_order_acquire),
                      kTwoDeliveredSampleCount);
            ++unset_calls;
            return score::Result<void>{};
        });
    EXPECT_CALL(*ev0, Unsubscribe())
        .Times(1)
        .InSequence(unsubscribe_seq)
        .WillOnce([&sync_mutex, &sync_cv, &in_get_new_samples, &in_update_event,
                   &sample_deleter_calls, &unsubscribe_calls, &unsubscribe_completed]() noexcept {
            {
                std::lock_guard<std::mutex> const lock{sync_mutex};
                EXPECT_FALSE(in_get_new_samples);
                EXPECT_FALSE(in_update_event);
                EXPECT_EQ(sample_deleter_calls.load(std::memory_order_acquire),
                          kTwoDeliveredSampleCount);
                ++unsubscribe_calls;
                unsubscribe_completed = true;
            }
            sync_cv.notify_all();
        });

    auto first_sample = MakeFramedSample<kMinPeerSampleSize24>(kPrefixLength4, kSignBitBytes);
    auto second_sample = MakeFramedSample<kMinPeerSampleSize24>(kPrefixLength4, kReconnectBytes);
    EXPECT_CALL(*ev0, GetNewSamples(_, _))
        .Times(1)
        .WillOnce([&sync_mutex, &in_get_new_samples, &get_new_samples_returned, &first_sample,
                   &second_sample](score::mw::com::impl::GenericProxyEventBinding::Callback&& cb,
                                   score::mw::com::impl::TrackerGuardFactory& tracker) {
            {
                std::lock_guard<std::mutex> const lock{sync_mutex};
                in_get_new_samples = true;
            }
            auto sample0 = MakeTrackedMockSample(first_sample.bytes.data(), tracker);
            auto sample1 = MakeTrackedMockSample(second_sample.bytes.data(), tracker);
            EXPECT_NE(sample0.get(), nullptr);
            EXPECT_NE(sample1.get(), nullptr);
            DeliverSample(cb, std::move(sample0));
            DeliverSample(cb, std::move(sample1));
            {
                std::lock_guard<std::mutex> const lock{sync_mutex};
                in_get_new_samples = false;
                get_new_samples_returned = true;
            }
            return score::Result<std::size_t>{kTwoDeliveredSampleCount};
        });

    std::vector<std::vector<std::byte>> forwarded_payloads{};
    EXPECT_CALL(*server_connector, update_event(kSocomEvent0, _))
        .Times(kSingleDeliveredSampleCount)
        .WillOnce([&sync_mutex, &in_get_new_samples, &get_new_samples_returned, &in_update_event,
                   &sample_deleter_calls, &forwarded_payloads, &server_callbacks,
                   server_connector](socom::Event_id, socom::Payload payload) {
            {
                std::lock_guard<std::mutex> const lock{sync_mutex};
                in_update_event = true;
                EXPECT_FALSE(in_get_new_samples);
                EXPECT_TRUE(get_new_samples_returned);
            }
            EXPECT_EQ(sample_deleter_calls.load(std::memory_order_acquire), kZeroDeliveredSamples);
            auto const span = payload.data();
            forwarded_payloads.emplace_back(span.begin(), span.end());
            server_callbacks.on_event_subscription_change(*server_connector, kSocomEvent0,
                                                          socom::Event_state::unsubscribed);
            {
                std::lock_guard<std::mutex> const lock{sync_mutex};
                in_update_event = false;
            }
            return score::Result<void>{};
        });

    auto const binding = StartAndSubscribeSingleEvent(server_callbacks, *server_connector);
    ASSERT_NE(binding, nullptr);

    auto const locked_handler = ev0_receive_handler.lock();
    ASSERT_NE(locked_handler, nullptr);
    (*locked_handler)();

    {
        std::unique_lock<std::mutex> lock{sync_mutex};
        ASSERT_TRUE(sync_cv.wait_until(
            lock, std::chrono::steady_clock::now() + kSynchronizationTimeout,
            [&unsubscribe_completed]() noexcept { return unsubscribe_completed; }));
        EXPECT_EQ(unset_calls, 1U);
        EXPECT_EQ(unsubscribe_calls, 1U);
    }

    ASSERT_EQ(forwarded_payloads.size(), kSingleDeliveredSampleCount);
    EXPECT_THAT(forwarded_payloads.front(), ElementsAreArray(kSignBitBytes));
    EXPECT_EQ(sample_deleter_calls.load(std::memory_order_acquire), kTwoDeliveredSampleCount);
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
