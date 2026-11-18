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

#include "score/gateway_ipc_binding/impl/provider_service_binding.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/gateway_ipc_binding/test/unit/ipc_binding_test_fixture.hpp"
#include "score/socom/client_connector.hpp"
#include "score/socom/error.hpp"
#include "score/socom/event.hpp"
#include "score/socom/payload.hpp"
#include "score/socom/service_interface_definition.hpp"
#include "score/test/mw_com_mocks.hpp"

namespace score::gateway_ipc_binding::mw_com {
namespace {

using ::testing::_;
using ::testing::AtLeast;
using ::testing::ByMove;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Expectation;
using ::testing::InSequence;
using ::testing::MockFunction;
using ::testing::NiceMock;
using ::testing::Ref;
using ::testing::Return;

constexpr socom::Event_id kSocomEvent0{0U};
constexpr socom::Event_id kSocomEvent1{1U};
constexpr socom::Event_id kOutOfRangeTwoEventId{2U};

constexpr std::size_t kPayloadLengthFieldSize{4U};
constexpr std::size_t kReservedPrefixOffset{4U};
constexpr std::size_t kReservedPrefixSize{4U};

// Hand-calculated sample geometry from the 8-byte prefix + 8-byte alignment specification:
// - Default single-event fixture: max_payload_size = 16 -> addressed = 24 -> sample_size = 24.
// - Two-event fixture event 0:    max_payload_size = 20 -> addressed = 28 -> sample_size = 32.
// - Two-event fixture event 1:    max_payload_size = 4  -> addressed = 12 -> sample_size = 16.
constexpr std::size_t kDefaultEvent0SampleSize{24U};

constexpr std::size_t kTwoEvent0MaxPayload{20U};
constexpr std::size_t kTwoEvent0SampleSize{32U};
constexpr std::size_t kTwoEvent0OversizedPayload{21U};
constexpr std::size_t kTwoEvent0AlignmentPaddingSize{4U};

constexpr std::size_t kTwoEvent1MaxPayload{4U};
constexpr std::size_t kTwoEvent1SampleSize{16U};
constexpr std::size_t kTwoEvent1EmptyTailSize{8U};

constexpr std::size_t kZeroPayloadCount{0U};
constexpr std::size_t kSignBitVectorLength{4U};
constexpr std::size_t kSignBitTailSize{20U};
constexpr std::size_t kConcurrentWorkerCount{3U};
constexpr std::size_t kConcurrentIterations{64U};
constexpr std::chrono::seconds kConcurrencyTimeout{5};
constexpr std::chrono::milliseconds kInFlightSendOverlapWindow{20};

constexpr std::byte kGarbageFillByte{0xDD};
constexpr std::byte kAltFillByte{0xAA};
constexpr std::byte kFullFillByte{0xEE};
constexpr std::byte kOversizedFillByte{0x5A};
constexpr std::byte kZeroByte{0x00};

constexpr std::array<std::byte, kSignBitVectorLength> kSignBitPayload{
    std::byte{0x00},
    std::byte{0x7F},
    std::byte{0x80},
    std::byte{0xFF},
};

constexpr std::array<std::byte, kTwoEvent0MaxPayload> kPinned20BytePayload{
    std::byte{0x80}, std::byte{0x81}, std::byte{0x82}, std::byte{0x83}, std::byte{0x84},
    std::byte{0x85}, std::byte{0x86}, std::byte{0x87}, std::byte{0x88}, std::byte{0x89},
    std::byte{0x8A}, std::byte{0x8B}, std::byte{0x8C}, std::byte{0x8D}, std::byte{0x8E},
    std::byte{0x8F}, std::byte{0x90}, std::byte{0x91}, std::byte{0x92}, std::byte{0x93},
};

constexpr std::array<std::byte, kReservedPrefixSize> kExpectedReservedZeros{
    kZeroByte,
    kZeroByte,
    kZeroByte,
    kZeroByte,
};

constexpr std::array<std::byte, kPayloadLengthFieldSize> kExpectedLength0Prefix{
    std::byte{0x00},
    std::byte{0x00},
    std::byte{0x00},
    std::byte{0x00},
};

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
constexpr std::array<std::byte, kPayloadLengthFieldSize> kExpectedLength4Prefix{
    std::byte{0x04},
    std::byte{0x00},
    std::byte{0x00},
    std::byte{0x00},
};
constexpr std::array<std::byte, kPayloadLengthFieldSize> kExpectedLength20Prefix{
    std::byte{0x14},
    std::byte{0x00},
    std::byte{0x00},
    std::byte{0x00},
};
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
constexpr std::array<std::byte, kPayloadLengthFieldSize> kExpectedLength4Prefix{
    std::byte{0x00},
    std::byte{0x00},
    std::byte{0x00},
    std::byte{0x04},
};
constexpr std::array<std::byte, kPayloadLengthFieldSize> kExpectedLength20Prefix{
    std::byte{0x00},
    std::byte{0x00},
    std::byte{0x00},
    std::byte{0x14},
};
#else
#error "Unsupported host byte order"
#endif

template <std::size_t SampleBytes>
struct TrackedSampleBuffer {
    alignas(kSample_alignment) std::array<std::byte, SampleBytes> bytes{};
    bool* released_flag{nullptr};
    std::vector<std::string>* teardown_trace{nullptr};
};

static_assert(std::is_standard_layout_v<TrackedSampleBuffer<kTwoEvent0SampleSize>>);
static_assert(offsetof(TrackedSampleBuffer<kTwoEvent0SampleSize>, bytes) == 0U);

template <std::size_t SampleBytes>
score::Result<score::mw::com::impl::SampleAllocateePtr<void>> MakeTrackedAllocatedSample(
    TrackedSampleBuffer<SampleBytes>& buffer,
    score::mw::com::impl::SampleAllocateeGuard guard) noexcept {
    score::mw::com::impl::mock_binding::SampleAllocateePtr mock_ptr{
        static_cast<void*>(&buffer), +[](void* const raw) noexcept {
            if (raw != nullptr) {
                auto* const tracked = static_cast<TrackedSampleBuffer<SampleBytes>*>(raw);
                if (tracked->released_flag != nullptr) {
                    *tracked->released_flag = true;
                }
                if (tracked->teardown_trace != nullptr) {
                    tracked->teardown_trace->emplace_back("allocation_released");
                }
            }
        }};
    return score::mw::com::impl::MakeSampleAllocateePtr(std::move(mock_ptr), std::move(guard));
}

class DestructorTrackingClientConnectorMock final
    : public ::testing::NiceMock<score::socom::Client_connector_mock> {
   public:
    explicit DestructorTrackingClientConnectorMock(std::vector<std::string>* const trace) noexcept
        : m_trace{trace} {}

    ~DestructorTrackingClientConnectorMock() noexcept override {
        if (m_trace != nullptr) {
            m_trace->emplace_back("connector_destroyed");
        }
    }

   private:
    std::vector<std::string>* m_trace;
};

class ScopedThread final {
   public:
    ScopedThread() noexcept = default;
    explicit ScopedThread(std::thread thread) noexcept : m_thread{std::move(thread)} {}

    ~ScopedThread() noexcept { Join(); }

    ScopedThread(ScopedThread const&) = delete;
    ScopedThread& operator=(ScopedThread const&) = delete;

    ScopedThread(ScopedThread&& other) noexcept : m_thread{std::move(other.m_thread)} {}
    ScopedThread& operator=(ScopedThread&& other) noexcept {
        if (this != &other) {
            Join();
            m_thread = std::move(other.m_thread);
        }
        return *this;
    }

    void Join() noexcept {
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

   private:
    std::thread m_thread{};
};

class PromiseGate final {
   public:
    PromiseGate() : m_future{m_promise.get_future().share()} {}

    PromiseGate(PromiseGate const&) = delete;
    PromiseGate& operator=(PromiseGate const&) = delete;
    PromiseGate(PromiseGate&&) = delete;
    PromiseGate& operator=(PromiseGate&&) = delete;

    void Signal() noexcept {
        std::call_once(m_once, [this]() { m_promise.set_value(); });
    }

    [[nodiscard]] std::shared_future<void> Future() const noexcept { return m_future; }

    template <typename Rep, typename Period>
    [[nodiscard]] bool WaitFor(std::chrono::duration<Rep, Period> const timeout) const {
        auto future_copy = m_future;
        return future_copy.wait_for(timeout) == std::future_status::ready;
    }

   private:
    std::promise<void> m_promise{};
    std::shared_future<void> m_future;
    std::once_flag m_once{};
};

class ScopedGateSignaler final {
   public:
    explicit ScopedGateSignaler(PromiseGate& gate) noexcept : m_gate{gate} {}
    ~ScopedGateSignaler() noexcept { m_gate.Signal(); }

    ScopedGateSignaler(ScopedGateSignaler const&) = delete;
    ScopedGateSignaler& operator=(ScopedGateSignaler const&) = delete;
    ScopedGateSignaler(ScopedGateSignaler&&) = delete;
    ScopedGateSignaler& operator=(ScopedGateSignaler&&) = delete;

   private:
    PromiseGate& m_gate;
};

class ConcurrentSampleArena final {
   public:
    static constexpr std::size_t kMaxSlotBytes{kTwoEvent0SampleSize};

    struct Slot {
        alignas(kSample_alignment) std::array<std::byte, kMaxSlotBytes> bytes{};
        ConcurrentSampleArena* owner{nullptr};
        bool active{false};
    };

    static_assert(std::is_standard_layout_v<Slot>);
    static_assert(offsetof(Slot, bytes) == 0U);
    static_assert(kDefaultEvent0SampleSize <= kMaxSlotBytes);
    static_assert(kTwoEvent1SampleSize <= kMaxSlotBytes);

    score::Result<score::mw::com::impl::SampleAllocateePtr<void>> Allocate(
        std::size_t const sample_bytes, score::mw::com::impl::SampleAllocateeGuard guard) {
        if (sample_bytes > kMaxSlotBytes) {
            return score::MakeUnexpected(score::mw::com::impl::ComErrc::kSampleAllocationFailure);
        }
        Slot* slot_ptr{nullptr};
        {
            std::lock_guard<std::mutex> const lock{m_mutex};
            auto slot = std::make_unique<Slot>();
            slot->owner = this;
            slot->active = true;
            slot_ptr = slot.get();
            m_slots.push_back(std::move(slot));
            ++m_active_slots;
        }
        score::mw::com::impl::mock_binding::SampleAllocateePtr mock_ptr{
            static_cast<void*>(slot_ptr), +[](void* const raw) noexcept {
                if (raw != nullptr) {
                    auto* const released_slot = static_cast<Slot*>(raw);
                    if (released_slot->owner != nullptr) {
                        released_slot->owner->ReleaseSlot(*released_slot);
                    }
                }
            }};
        return score::mw::com::impl::MakeSampleAllocateePtr(std::move(mock_ptr), std::move(guard));
    }

    [[nodiscard]] std::size_t active_slots() const noexcept {
        std::lock_guard<std::mutex> const lock{m_mutex};
        return m_active_slots;
    }

   private:
    void ReleaseSlot(Slot& slot) noexcept {
        std::lock_guard<std::mutex> const lock{m_mutex};
        if (slot.active) {
            slot.active = false;
            if (m_active_slots > 0U) {
                --m_active_slots;
            }
        }
    }

    mutable std::mutex m_mutex{};
    std::vector<std::unique_ptr<Slot>> m_slots{};
    std::size_t m_active_slots{0U};
};

class ConcurrentFakeSkeleton final : public score::mw::com::impl::SkeletonBinding {
   public:
    score::Result<void> PrepareOffer(SkeletonEventBindings&, SkeletonFieldBindings&,
                                     std::optional<RegisterShmObjectTraceCallback>) override {
        return {};
    }

    void PrepareStopOffer(std::optional<UnregisterShmObjectTraceCallback>) override {}

    score::mw::com::impl::BindingType GetBindingType() const noexcept override {
        return score::mw::com::impl::BindingType::kLoLa;
    }

    bool VerifyAllMethodHandlersRegistered() const override { return true; }
};

class ConcurrentFakeSkeletonEvent final : public score::mw::com::impl::GenericSkeletonEventBinding {
   public:
    ConcurrentFakeSkeletonEvent(
        std::size_t const sample_bytes, std::shared_ptr<ConcurrentSampleArena> sample_arena,
        std::shared_ptr<std::mutex> callback_mutex,
        std::shared_ptr<score::mw::com::impl::ReceiveHandlerRegistrationChangedCallback>
            shared_callback,
        std::atomic<std::size_t>* const sent_count) noexcept
        : m_sample_bytes{sample_bytes},
          m_sample_arena{std::move(sample_arena)},
          m_callback_mutex{std::move(callback_mutex)},
          m_shared_callback{std::move(shared_callback)},
          m_sent_count{sent_count} {}

    score::Result<void> Send(score::mw::com::impl::SampleAllocateePtr<void> sample,
                             std::optional<SendTraceCallback>) override {
        if (sample.Get() != nullptr && m_sent_count != nullptr) {
            m_sent_count->fetch_add(1U, std::memory_order_relaxed);
        }
        return {};
    }

    score::Result<score::mw::com::impl::SampleAllocateePtr<void>> Allocate(
        score::mw::com::impl::SampleAllocateeGuard guard) override {
        return m_sample_arena->Allocate(m_sample_bytes, std::move(guard));
    }

    score::Result<score::mw::com::impl::SamplePtr<void>> GetLatestSample(
        score::mw::com::impl::QualityType) override {
        return score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure);
    }

    score::Result<void> PrepareOffer(
        std::optional<score::mw::com::impl::InitializeSampleCallback> const&) override {
        return {};
    }

    void PrepareStopOffer() override {}

    score::memory::DataTypeSizeInfo GetEventDataTypeSizeInfo() const override {
        return score::memory::DataTypeSizeInfo{m_sample_bytes, kSample_alignment};
    }

    score::mw::com::impl::BindingType GetBindingType() const noexcept override {
        return score::mw::com::impl::BindingType::kLoLa;
    }

    void SetSkeletonEventTracingData(
        score::mw::com::impl::tracing::SkeletonEventTracingData) noexcept override {}

    score::Result<void> Notify() override { return {}; }

    score::Result<void> SetReceiveHandlerRegistrationChangedHandler(
        score::mw::com::impl::ReceiveHandlerRegistrationChangedCallback callback) noexcept
        override {
        std::lock_guard<std::mutex> const lock{*m_callback_mutex};
        *m_shared_callback = std::move(callback);
        return {};
    }

    score::Result<void> UnsetReceiveHandlerRegistrationChangedHandler() noexcept override {
        std::lock_guard<std::mutex> const lock{*m_callback_mutex};
        *m_shared_callback = score::mw::com::impl::ReceiveHandlerRegistrationChangedCallback{};
        return {};
    }

   private:
    std::size_t m_sample_bytes;
    std::shared_ptr<ConcurrentSampleArena> m_sample_arena;
    std::shared_ptr<std::mutex> m_callback_mutex;
    std::shared_ptr<score::mw::com::impl::ReceiveHandlerRegistrationChangedCallback>
        m_shared_callback;
    std::atomic<std::size_t>* m_sent_count;
};

class ConcurrentFakeClientConnector final : public socom::Client_connector {
   public:
    ConcurrentFakeClientConnector(socom::Service_interface_definition config,
                                  socom::Service_instance instance) noexcept
        : m_config{std::move(config)}, m_instance{std::move(instance)} {}

    score::Result<socom::Writable_payload> allocate_method_call_payload(
        socom::Method_id) noexcept override {
        return score::MakeUnexpected(socom::Error::runtime_error_request_rejected);
    }

    score::Result<void> subscribe_event(socom::Event_id,
                                        socom::Event_mode) const noexcept override {
        return {};
    }

    score::Result<void> unsubscribe_event(socom::Event_id) const noexcept override { return {}; }

    score::Result<void> request_event_update(socom::Event_id) const noexcept override { return {}; }

    score::Result<socom::Method_invocation::Uptr> call_method(
        socom::Method_id, socom::Payload,
        socom::Method_call_reply_data_opt) const noexcept override {
        return score::MakeUnexpected(socom::Error::runtime_error_request_rejected);
    }

    socom::Service_interface_definition const& get_configuration() const noexcept override {
        return m_config;
    }

    socom::Service_instance const& get_service_instance() const noexcept override {
        return m_instance;
    }

    bool is_service_available() const noexcept override { return true; }

   private:
    socom::Service_interface_definition m_config;
    socom::Service_instance m_instance;
};

class ProviderServiceBindingTest : public IpcBindingTestFixture {
   protected:
    using PeerInterestHandler = score::mw::com::impl::ReceiveHandlerRegistrationChangedCallback;

    struct PreparedProviderSkeleton {
        NiceMock<score::testing::Skeleton>* skeleton{nullptr};
        std::vector<NiceMock<score::testing::GenericSkeletonEvent>*> events{};
        std::shared_ptr<std::vector<PeerInterestHandler>> peer_handlers{};
    };

    static Service_config MakeTwoEventProviderConfig(
        std::size_t const max_sample_count = kDefaultMaxSampleCount) {
        return MakeMultiEventServiceConfig(
            Role::provider,
            {
                Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id},
                             kTwoEvent0MaxPayload},
                Event_config{std::string{kEvent1}, Mw_com_event_id{kEvent1Id},
                             kTwoEvent1MaxPayload},
            },
            kValidBridgedSpecifier, kDefaultServiceId, max_sample_count);
    }

    static socom::Server_service_interface_definition MakeServerDefinition(
        Service_config const& config) {
        return socom::Server_service_interface_definition{
            config.interface, socom::to_num_of_methods(0U),
            socom::to_num_of_events(config.events.size())};
    }

    void StubProviderEventsResolved(Service_config const& config) {
        if (config.events.size() == 1U) {
            StubInstanceResolved(config.instance_specifier, {config.events[0].name},
                                 config.service_id, config.instance_id);
        } else if (config.events.size() == 2U) {
            StubInstanceResolved(config.instance_specifier,
                                 {config.events[0].name, config.events[1].name}, config.service_id,
                                 config.instance_id);
        }
    }

    PreparedProviderSkeleton ExpectPreparedSkeleton(
        Service_config const& config, std::vector<std::size_t> const& laid_out_sample_sizes) {
        StubProviderEventsResolved(config);

        auto skel_mock = std::make_unique<NiceMock<score::testing::Skeleton>>();
        auto* const skel_ptr = skel_mock.get();
        auto handlers = std::make_shared<std::vector<PeerInterestHandler>>(config.events.size());

        PreparedProviderSkeleton prepared{skel_ptr, {}, handlers};
        prepared.events.reserve(config.events.size());

        EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
            .InSequence(skeleton_factory_sequence())
            .WillOnce(Return(ByMove(std::move(skel_mock))))
            .RetiresOnSaturation();

        for (std::size_t idx = 0U; idx < config.events.size(); ++idx) {
            auto event_mock = std::make_unique<NiceMock<score::testing::GenericSkeletonEvent>>();
            auto* const event_ptr = event_mock.get();
            auto const sample_bytes = laid_out_sample_sizes[idx];
            auto const size_info_alignment =
                ((sample_bytes % kSample_alignment) == 0U) ? kSample_alignment : 1U;

            ON_CALL(*event_ptr, GetEventDataTypeSizeInfo())
                .WillByDefault(
                    Return(score::memory::DataTypeSizeInfo{sample_bytes, size_info_alignment}));
            ON_CALL(*event_ptr, SetReceiveHandlerRegistrationChangedHandler(_))
                .WillByDefault([handlers, idx](PeerInterestHandler callback) noexcept {
                    (*handlers)[idx] = std::move(callback);
                    return score::Result<void>{};
                });

            prepared.events.push_back(event_ptr);
            EXPECT_CALL(generic_skeleton_event_factory_mock(),
                        Create(_, Ref(*skel_ptr), config.events[idx].name, _))
                .WillOnce(Return(ByMove(std::move(event_mock))))
                .RetiresOnSaturation();
        }

        return prepared;
    }

    void ExpectNoStopOfferCalls(NiceMock<score::testing::Skeleton>& skeleton) {
        EXPECT_CALL(skeleton, PrepareStopOffer(_)).Times(0);
        EXPECT_CALL(service_discovery_mock(), StopOfferService(_)).Times(0);
        EXPECT_CALL(service_discovery_mock(), StopOfferService(_, _)).Times(0);
    }
};

// ============================================================================
// 1. Provider_service_binding::create() & sample-size validation
// ============================================================================

TEST_F(ProviderServiceBindingTest, CreateRejectsInvalidInstanceSpecifier) {
    constexpr std::array<std::string_view, 2U> kInvalidSpecifiers{
        "",
        kInvalidBridgedSpecifier,
    };
    ASSERT_EQ(kInvalidSpecifiers.size(), 2U);

    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_)).Times(0);
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);

    for (auto const invalid_specifier : kInvalidSpecifiers) {
        SCOPED_TRACE(invalid_specifier);
        auto const config = MakeServiceConfig(Role::provider, invalid_specifier);
        auto result = Provider_service_binding::create(socom_runtime_, config);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), Mw_com_binding_error::logic_error_invalid_instance_specifier);
    }
}

TEST_F(ProviderServiceBindingTest, CreateRejectsSkeletonSampleSizeMismatch) {
    struct SizeMismatchCase {
        std::string_view label;
        std::size_t event0_size;
        std::size_t event1_size;
    };
    constexpr std::array<SizeMismatchCase, 3U> kCases{{
        {"event0 undersized by 1 byte", kTwoEvent0SampleSize - 1U, kTwoEvent1SampleSize},
        {"event0 oversized by alignment step", kTwoEvent0SampleSize + kSample_alignment,
         kTwoEvent1SampleSize},
        {"event1 (2 of 2) undersized by 1 byte", kTwoEvent0SampleSize, kTwoEvent1SampleSize - 1U},
    }};
    ASSERT_EQ(kCases.size(), 3U);

    auto const config = MakeTwoEventProviderConfig();
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);

    for (auto const& test_case : kCases) {
        auto prepared =
            ExpectPreparedSkeleton(config, {test_case.event0_size, test_case.event1_size});
        for (auto* const event_mock : prepared.events) {
            EXPECT_CALL(*event_mock, SetReceiveHandlerRegistrationChangedHandler(_)).Times(0);
        }
    }

    for (auto const& test_case : kCases) {
        SCOPED_TRACE(test_case.label);
        auto result = Provider_service_binding::create(socom_runtime_, config);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), Mw_com_binding_error::runtime_error_sample_size_mismatch);
    }
}

TEST_F(ProviderServiceBindingTest, CreateFailsWhenSkeletonCreationFails) {
    auto const config = MakeServiceConfig(Role::provider);
    StubProviderEventsResolved(config);
    ExpectSkeletonCreationFailure();
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);

    auto result = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::runtime_error_service_setup_failed);
}

TEST_F(ProviderServiceBindingTest, CreateFailsWhenSecondSkeletonEventCreationFails) {
    auto const config = MakeTwoEventProviderConfig();
    StubProviderEventsResolved(config);

    auto skel_mock = std::make_unique<NiceMock<score::testing::Skeleton>>();
    auto* const skel_ptr = skel_mock.get();
    auto event0_mock = std::make_unique<NiceMock<score::testing::GenericSkeletonEvent>>();
    ON_CALL(*event0_mock, GetEventDataTypeSizeInfo())
        .WillByDefault(
            Return(score::memory::DataTypeSizeInfo{kTwoEvent0SampleSize, kSample_alignment}));

    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
        .InSequence(skeleton_factory_sequence())
        .WillOnce(Return(ByMove(std::move(skel_mock))))
        .RetiresOnSaturation();
    EXPECT_CALL(generic_skeleton_event_factory_mock(),
                Create(_, Ref(*skel_ptr), kEvent0,
                       score::memory::DataTypeSizeInfo{kTwoEvent0SampleSize, kSample_alignment}))
        .WillOnce(Return(ByMove(std::move(event0_mock))))
        .RetiresOnSaturation();
    EXPECT_CALL(generic_skeleton_event_factory_mock(),
                Create(_, Ref(*skel_ptr), kEvent1,
                       score::memory::DataTypeSizeInfo{kTwoEvent1SampleSize, kSample_alignment}))
        .WillOnce(Return(ByMove(std::unique_ptr<score::mw::com::impl::SkeletonEventBinding>{})))
        .RetiresOnSaturation();
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);

    auto result = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::runtime_error_service_setup_failed);
}

TEST_F(ProviderServiceBindingTest,
       CreateUnsetsEarlierHandlersWhenSecondEventHandlerRegistrationFails) {
    auto const config = MakeTwoEventProviderConfig();
    auto prepared = ExpectPreparedSkeleton(config, {kTwoEvent0SampleSize, kTwoEvent1SampleSize});
    ASSERT_EQ(prepared.events.size(), 2U);

    EXPECT_CALL(*prepared.events[0], SetReceiveHandlerRegistrationChangedHandler(_))
        .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*prepared.events[1], SetReceiveHandlerRegistrationChangedHandler(_))
        .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)));
    EXPECT_CALL(*prepared.events[0], UnsetReceiveHandlerRegistrationChangedHandler())
        .Times(AtLeast(1));
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);

    auto result = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::runtime_error_service_setup_failed);
}

TEST_F(ProviderServiceBindingTest, CreateUnsetsHandlersWhenClientConnectorCreationFails) {
    auto const config = MakeTwoEventProviderConfig(kZeroPayloadCount);
    auto prepared = ExpectPreparedSkeleton(config, {kTwoEvent0SampleSize, kTwoEvent1SampleSize});
    ASSERT_EQ(prepared.events.size(), 2U);

    EXPECT_CALL(*prepared.events[0], UnsetReceiveHandlerRegistrationChangedHandler())
        .Times(AtLeast(1));
    EXPECT_CALL(*prepared.events[1], UnsetReceiveHandlerRegistrationChangedHandler())
        .Times(AtLeast(1));
    ExpectNoStopOfferCalls(*prepared.skeleton);
    EXPECT_CALL(service_discovery_mock(), OfferService(_)).Times(0);
    ExpectClientConnectorCreationFailure(socom::Error::runtime_error_request_rejected);

    auto result = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Mw_com_binding_error::runtime_error_service_setup_failed);
}

TEST_F(ProviderServiceBindingTest, CreateConfiguresClientConnectorWithoutOfferingUntilAvailable) {
    auto const config = MakeTwoEventProviderConfig(kZeroPayloadCount);
    auto prepared = ExpectPreparedSkeleton(config, {kTwoEvent0SampleSize, kTwoEvent1SampleSize});
    ASSERT_EQ(prepared.events.size(), 2U);

    socom::Client_connector::Callbacks callbacks{};
    auto cc_mock = std::make_unique<NiceMock<socom::Client_connector_mock>>();
    auto* const cc_ptr = cc_mock.get();

    std::optional<socom::Service_interface_definition> captured_def{};
    std::optional<socom::Service_instance> captured_instance{};
    EXPECT_CALL(socom_runtime_, make_client_connector(_, _, _))
        .InSequence(client_connector_sequence())
        .WillOnce([cc = std::move(cc_mock), &callbacks, &captured_def, &captured_instance](
                      socom::Service_interface_definition definition,
                      socom::Service_instance instance,
                      socom::Client_connector::Callbacks cb) mutable
                      -> score::Result<socom::Client_connector::Uptr> {
            captured_def.emplace(std::move(definition));
            captured_instance.emplace(std::move(instance));
            callbacks = std::move(cb);
            return std::move(cc);
        })
        .RetiresOnSaturation();

    MockFunction<void(std::string_view)> phase;
    Expectation const become_available = EXPECT_CALL(phase, Call("become_available"));
    Expectation const offer_skeleton = EXPECT_CALL(*prepared.skeleton, PrepareOffer(_, _, _))
                                           .After(become_available)
                                           .WillOnce(Return(score::Result<void>{}));
    Expectation const offer_discovery = EXPECT_CALL(service_discovery_mock(), OfferService(_))
                                            .After(become_available)
                                            .WillOnce(Return(score::Result<void>{}));
    Expectation const teardown =
        EXPECT_CALL(phase, Call("teardown")).After(offer_skeleton, offer_discovery);
    EXPECT_CALL(*prepared.skeleton, PrepareStopOffer(_)).Times(1).After(teardown);

    EXPECT_CALL(*prepared.events[0], Allocate(_)).Times(0);
    EXPECT_CALL(*prepared.events[0], Send(_, _)).Times(0);
    EXPECT_CALL(*prepared.events[1], Allocate(_)).Times(0);
    EXPECT_CALL(*prepared.events[1], Send(_, _)).Times(0);

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);

    ASSERT_TRUE(captured_def.has_value());
    EXPECT_EQ(captured_def->interface, config.interface);
    EXPECT_EQ(captured_def->num_methods, 0U);
    ASSERT_TRUE(captured_instance.has_value());
    EXPECT_EQ(*captured_instance, config.instance);

    EXPECT_TRUE(binding->start().has_value());
    EXPECT_TRUE(binding->start().has_value());

    auto const server_def = MakeServerDefinition(config);
    // Initial not_available before ever offering and requested-update callback are safe no-ops.
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::not_available, server_def);
    callbacks.on_event_requested_update(*cc_ptr, kSocomEvent0, socom::empty_payload());

    phase.Call("become_available");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);
    // Duplicate available while already offered is a no-op.
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    phase.Call("teardown");
    binding.reset();
}

// ============================================================================
// 2. Valid -> Error -> Valid Offer / Recreation & Skeleton Retention Lifecycle
// ============================================================================

TEST_F(ProviderServiceBindingTest, FailedOfferRollsBackAndAllowsDirectAvailableRetry) {
    auto const config = MakeServiceConfig(Role::provider);
    auto prepared = ExpectPreparedSkeleton(config, {kDefaultEvent0SampleSize});
    ASSERT_EQ(prepared.events.size(), 1U);

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    MockFunction<void(std::string_view)> phase;
    Expectation const first_offer_fails =
        EXPECT_CALL(*prepared.skeleton, PrepareOffer(_, _, _))
            .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)))
            .RetiresOnSaturation();
    Expectation const peer_wants =
        EXPECT_CALL(phase, Call("peer_wants_while_unoffered")).After(first_offer_fails);
    Expectation const direct_retry =
        EXPECT_CALL(phase, Call("direct_available_retry")).After(peer_wants);
    Expectation const retry_prepare = EXPECT_CALL(*prepared.skeleton, PrepareOffer(_, _, _))
                                          .After(direct_retry)
                                          .WillOnce(Return(score::Result<void>{}));
    Expectation const retry_offer = EXPECT_CALL(service_discovery_mock(), OfferService(_))
                                        .After(direct_retry)
                                        .WillOnce(Return(score::Result<void>{}));
    Expectation const retry_subscribe =
        EXPECT_CALL(*cc_ptr, subscribe_event(kSocomEvent0, socom::Event_mode::update))
            .After(direct_retry)
            .WillOnce(Return(score::Result<void>{}));
    Expectation const teardown =
        EXPECT_CALL(phase, Call("teardown")).After(retry_prepare, retry_offer, retry_subscribe);
    EXPECT_CALL(*prepared.skeleton, PrepareStopOffer(_)).Times(1).After(teardown);

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    auto const server_def = MakeServerDefinition(config);

    // 1. First OfferService() fails via PrepareOffer(): StopOfferService must NOT run.
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    // 2. Peer interest arriving while unoffered must NOT subscribe on SOCom.
    phase.Call("peer_wants_while_unoffered");
    ASSERT_FALSE(prepared.peer_handlers->at(0).empty());
    prepared.peer_handlers->at(0)(true);

    // 3. Direct retry of available (without an intervening not_available!) offers and subscribes.
    phase.Call("direct_available_retry");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    phase.Call("teardown");
    binding.reset();
}

TEST_F(ProviderServiceBindingTest, NotAvailableAfterFailedOfferIsSafeNoOp) {
    auto const config = MakeServiceConfig(Role::provider);
    auto prepared = ExpectPreparedSkeleton(config, {kDefaultEvent0SampleSize});

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    MockFunction<void(std::string_view)> phase;
    Expectation const first_offer_fails =
        EXPECT_CALL(*prepared.skeleton, PrepareOffer(_, _, _))
            .WillOnce(Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure)))
            .RetiresOnSaturation();
    Expectation const not_avail =
        EXPECT_CALL(phase, Call("not_available_after_failed_offer")).After(first_offer_fails);
    Expectation const recover =
        EXPECT_CALL(phase, Call("recover_on_next_available")).After(not_avail);
    Expectation const second_prepare = EXPECT_CALL(*prepared.skeleton, PrepareOffer(_, _, _))
                                           .After(recover)
                                           .WillOnce(Return(score::Result<void>{}));
    Expectation const second_offer = EXPECT_CALL(service_discovery_mock(), OfferService(_))
                                         .After(recover)
                                         .WillOnce(Return(score::Result<void>{}));
    Expectation const teardown =
        EXPECT_CALL(phase, Call("teardown")).After(second_prepare, second_offer);
    EXPECT_CALL(*prepared.skeleton, PrepareStopOffer(_)).Times(1).After(teardown);

    EXPECT_CALL(*cc_ptr, subscribe_event(_, _)).Times(0);

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    auto const server_def = MakeServerDefinition(config);

    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    // Must NOT call StopOfferService/PrepareStopOffer or destroy the existing skeleton.
    phase.Call("not_available_after_failed_offer");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::not_available, server_def);

    phase.Call("recover_on_next_available");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    phase.Call("teardown");
    binding.reset();
}

TEST_F(ProviderServiceBindingTest,
       NotAvailableWithoutPeerInterestDestroysSkeletonAndRecoversAfterFailedRecreation) {
    auto const config = MakeServiceConfig(Role::provider);
    auto first_skeleton = ExpectPreparedSkeleton(config, {kDefaultEvent0SampleSize});
    ExpectSkeletonCreationFailure();
    auto second_skeleton = ExpectPreparedSkeleton(config, {kDefaultEvent0SampleSize});

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    MockFunction<void(std::string_view)> phase;
    Expectation const first_prepare = EXPECT_CALL(*first_skeleton.skeleton, PrepareOffer(_, _, _))
                                          .WillOnce(Return(score::Result<void>{}));
    Expectation const first_offer = EXPECT_CALL(service_discovery_mock(), OfferService(_))
                                        .WillOnce(Return(score::Result<void>{}))
                                        .RetiresOnSaturation();
    Expectation const withdraw_first =
        EXPECT_CALL(phase, Call("withdraw_and_destroy_first_skeleton"))
            .After(first_prepare, first_offer);
    Expectation const first_stop =
        EXPECT_CALL(*first_skeleton.skeleton, PrepareStopOffer(_)).Times(1).After(withdraw_first);
    Expectation const recreate_fails =
        EXPECT_CALL(phase, Call("recreate_fails_on_available")).After(first_stop);
    Expectation const not_avail =
        EXPECT_CALL(phase, Call("not_available_after_failed_recreate")).After(recreate_fails);
    Expectation const recreate_succeeds =
        EXPECT_CALL(phase, Call("recreate_succeeds_on_available")).After(not_avail);
    Expectation const second_prepare = EXPECT_CALL(*second_skeleton.skeleton, PrepareOffer(_, _, _))
                                           .After(recreate_succeeds)
                                           .WillOnce(Return(score::Result<void>{}));
    Expectation const second_offer = EXPECT_CALL(service_discovery_mock(), OfferService(_))
                                         .After(recreate_succeeds)
                                         .WillOnce(Return(score::Result<void>{}));
    Expectation const teardown =
        EXPECT_CALL(phase, Call("teardown")).After(second_prepare, second_offer);
    EXPECT_CALL(*second_skeleton.skeleton, PrepareStopOffer(_)).Times(1).After(teardown);

    EXPECT_CALL(*first_skeleton.events[0], UnsetReceiveHandlerRegistrationChangedHandler())
        .Times(AtLeast(1));
    EXPECT_CALL(*first_skeleton.events[0], Allocate(_)).Times(0);

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    auto const server_def = MakeServerDefinition(config);

    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    phase.Call("withdraw_and_destroy_first_skeleton");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::not_available, server_def);

    // While skeleton is torn down, payload allocation must fail cleanly.
    auto alloc_while_torn_down = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
    ASSERT_FALSE(alloc_while_torn_down.has_value());
    EXPECT_EQ(alloc_while_torn_down.error(),
              Mw_com_binding_error::runtime_error_sample_allocation_failed);

    // Recreation fails (GenericSkeleton::Create returns error -> m_skeleton stays std::nullopt).
    phase.Call("recreate_fails_on_available");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    // Subsequent not_available must NOT dereference an empty std::optional<GenericSkeleton>.
    phase.Call("not_available_after_failed_recreate");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::not_available, server_def);

    // Subsequent available recreates the skeleton and offers it cleanly.
    phase.Call("recreate_succeeds_on_available");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    phase.Call("teardown");
    binding.reset();
}

TEST_F(ProviderServiceBindingTest, DirectAvailableRetryAfterFailedSkeletonRecreationSucceeds) {
    auto const config = MakeServiceConfig(Role::provider);
    auto first_skeleton = ExpectPreparedSkeleton(config, {kDefaultEvent0SampleSize});
    ExpectSkeletonCreationFailure();
    auto second_skeleton = ExpectPreparedSkeleton(config, {kDefaultEvent0SampleSize});

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    MockFunction<void(std::string_view)> phase;
    Expectation const first_prepare = EXPECT_CALL(*first_skeleton.skeleton, PrepareOffer(_, _, _))
                                          .WillOnce(Return(score::Result<void>{}));
    Expectation const first_offer = EXPECT_CALL(service_discovery_mock(), OfferService(_))
                                        .WillOnce(Return(score::Result<void>{}))
                                        .RetiresOnSaturation();
    Expectation const first_stop = EXPECT_CALL(*first_skeleton.skeleton, PrepareStopOffer(_))
                                       .Times(1)
                                       .After(first_prepare, first_offer);
    Expectation const recreate_fails = EXPECT_CALL(phase, Call("recreate_fails")).After(first_stop);
    Expectation const direct_retry =
        EXPECT_CALL(phase, Call("direct_available_retry")).After(recreate_fails);
    Expectation const second_prepare = EXPECT_CALL(*second_skeleton.skeleton, PrepareOffer(_, _, _))
                                           .After(direct_retry)
                                           .WillOnce(Return(score::Result<void>{}));
    Expectation const second_offer = EXPECT_CALL(service_discovery_mock(), OfferService(_))
                                         .After(direct_retry)
                                         .WillOnce(Return(score::Result<void>{}));
    EXPECT_CALL(*second_skeleton.skeleton, PrepareStopOffer(_))
        .Times(1)
        .After(second_prepare, second_offer);

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    auto const server_def = MakeServerDefinition(config);

    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::not_available, server_def);

    phase.Call("recreate_fails");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    // Direct available retry without an intervening not_available must recreate and offer.
    phase.Call("direct_available_retry");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    binding.reset();
}

TEST_F(ProviderServiceBindingTest,
       NotAvailableWithPeerInterestRetainsSkeletonAndResubscribesOnReconnect) {
    auto const config = MakeTwoEventProviderConfig();
    // Only ONE skeleton creation expectation is registered: reconnect must retain it!
    auto prepared = ExpectPreparedSkeleton(config, {kTwoEvent0SampleSize, kTwoEvent1SampleSize});
    ASSERT_EQ(prepared.events.size(), 2U);

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    MockFunction<void(std::string_view)> phase;
    Expectation const first_avail = EXPECT_CALL(phase, Call("first_available"));
    Expectation const first_prepare = EXPECT_CALL(*prepared.skeleton, PrepareOffer(_, _, _))
                                          .After(first_avail)
                                          .WillOnce(Return(score::Result<void>{}))
                                          .RetiresOnSaturation();
    Expectation const first_offer = EXPECT_CALL(service_discovery_mock(), OfferService(_))
                                        .After(first_avail)
                                        .WillOnce(Return(score::Result<void>{}))
                                        .RetiresOnSaturation();
    Expectation const first_sub =
        EXPECT_CALL(*cc_ptr, subscribe_event(kSocomEvent0, socom::Event_mode::update))
            .After(first_avail)
            .WillOnce(Return(score::Result<void>{}))
            .RetiresOnSaturation();

    Expectation const temp_not_avail = EXPECT_CALL(phase, Call("temporary_not_available"))
                                           .After(first_prepare, first_offer, first_sub);
    Expectation const first_stop = EXPECT_CALL(*prepared.skeleton, PrepareStopOffer(_))
                                       .Times(1)
                                       .After(temp_not_avail)
                                       .RetiresOnSaturation();

    Expectation const reconnect_avail =
        EXPECT_CALL(phase, Call("reconnect_available")).After(first_stop);
    Expectation const second_prepare = EXPECT_CALL(*prepared.skeleton, PrepareOffer(_, _, _))
                                           .After(reconnect_avail)
                                           .WillOnce(Return(score::Result<void>{}));
    Expectation const second_offer = EXPECT_CALL(service_discovery_mock(), OfferService(_))
                                         .After(reconnect_avail)
                                         .WillOnce(Return(score::Result<void>{}));
    Expectation const second_sub =
        EXPECT_CALL(*cc_ptr, subscribe_event(kSocomEvent0, socom::Event_mode::update))
            .After(reconnect_avail)
            .WillOnce(Return(score::Result<void>{}));

    Expectation const teardown =
        EXPECT_CALL(phase, Call("teardown")).After(second_prepare, second_offer, second_sub);
    EXPECT_CALL(*prepared.skeleton, PrepareStopOffer(_)).Times(1).After(teardown);

    EXPECT_CALL(*cc_ptr, subscribe_event(kSocomEvent1, _)).Times(0);
    // SOCom drops subscriptions automatically when leaving available.
    EXPECT_CALL(*cc_ptr, unsubscribe_event(_)).Times(0);

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    auto const server_def = MakeServerDefinition(config);

    // Peer expresses interest only on event 0 before service becomes available.
    ASSERT_FALSE(prepared.peer_handlers->at(0).empty());
    prepared.peer_handlers->at(0)(true);

    phase.Call("first_available");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    phase.Call("temporary_not_available");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::not_available, server_def);

    phase.Call("reconnect_available");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    phase.Call("teardown");
    binding.reset();
}

// ============================================================================
// 3. Demand-driven SOCom subscription reconciliation & re-entrancy
// ============================================================================

TEST_F(ProviderServiceBindingTest,
       ReconcilesPeerInterestAndAvailabilityInBothOrdersAndRetriesFailedSubscribe) {
    auto const config = MakeServiceConfig(Role::provider);
    auto prepared = ExpectPreparedSkeleton(config, {kDefaultEvent0SampleSize});

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    MockFunction<void(std::string_view)> phase;
    {
        InSequence seq;
        EXPECT_CALL(phase, Call("toggle_interest_while_unavailable"));
        EXPECT_CALL(phase, Call("available_without_interest"));
        EXPECT_CALL(service_discovery_mock(), OfferService(_))
            .WillOnce(Return(score::Result<void>{}));

        EXPECT_CALL(phase, Call("first_subscribe_fails"));
        EXPECT_CALL(*cc_ptr, subscribe_event(kSocomEvent0, socom::Event_mode::update))
            .WillOnce(Return(score::MakeUnexpected(socom::Error::runtime_error_request_rejected)));

        EXPECT_CALL(phase, Call("drop_interest_after_failed_subscribe"));
        EXPECT_CALL(phase, Call("retry_subscribe_succeeds"));
        EXPECT_CALL(*cc_ptr, subscribe_event(kSocomEvent0, socom::Event_mode::update))
            .WillOnce(Return(score::Result<void>{}));

        EXPECT_CALL(phase, Call("duplicate_interest_true"));
        EXPECT_CALL(phase, Call("interest_false_unsubscribes"));
        EXPECT_CALL(*cc_ptr, unsubscribe_event(kSocomEvent0))
            .WillOnce(Return(score::Result<void>{}));

        EXPECT_CALL(phase, Call("duplicate_interest_false"));
    }

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    auto const server_def = MakeServerDefinition(config);
    auto& peer_handler = prepared.peer_handlers->at(0);
    ASSERT_FALSE(peer_handler.empty());

    // Interest asserted then withdrawn before available must neither subscribe nor unsubscribe.
    phase.Call("toggle_interest_while_unavailable");
    peer_handler(true);
    peer_handler(false);

    phase.Call("available_without_interest");
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    // Failed subscribe_event must NOT mark the event subscribed.
    phase.Call("first_subscribe_fails");
    peer_handler(true);

    // Dropping interest after failed subscribe must NOT call unsubscribe_event.
    phase.Call("drop_interest_after_failed_subscribe");
    peer_handler(false);

    // Re-asserting interest retries subscribe_event and succeeds.
    phase.Call("retry_subscribe_succeeds");
    peer_handler(true);

    // Duplicate true is idempotent.
    phase.Call("duplicate_interest_true");
    peer_handler(true);

    // Withdrawing interest while available unsubscribes once.
    phase.Call("interest_false_unsubscribes");
    peer_handler(false);

    // Duplicate false is idempotent.
    phase.Call("duplicate_interest_false");
    peer_handler(false);
}

TEST_F(ProviderServiceBindingTest,
       SynchronousEventDeliveryInsideSubscribeEventSucceedsWithoutDeadlock) {
    auto const config = MakeServiceConfig(Role::provider);
    auto prepared = ExpectPreparedSkeleton(config, {kDefaultEvent0SampleSize});

    alignas(kSample_alignment) std::array<std::byte, kDefaultEvent0SampleSize> sample_buffer{};
    sample_buffer.fill(kGarbageFillByte);

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    EXPECT_CALL(*prepared.events[0], Allocate(_))
        .WillOnce([&sample_buffer](score::mw::com::impl::SampleAllocateeGuard guard) {
            return score::testing::MakeAllocatedSample(sample_buffer.data(), std::move(guard));
        });

    void const* sent_sample_ptr{nullptr};
    EXPECT_CALL(*prepared.events[0], Send(_, _))
        .WillOnce([&sent_sample_ptr](
                      score::mw::com::impl::SampleAllocateePtr<void> sample,
                      std::optional<
                          score::mw::com::impl::GenericSkeletonEventBinding::SendTraceCallback>) {
            sent_sample_ptr = sample.Get();
            return score::Result<void>{};
        });

    EXPECT_CALL(*cc_ptr, subscribe_event(kSocomEvent0, socom::Event_mode::update))
        .WillOnce([&callbacks, cc_ptr](socom::Event_id const event_id, socom::Event_mode) noexcept {
            auto alloc = callbacks.on_event_payload_allocate(*cc_ptr, event_id);
            if (alloc.has_value()) {
                std::copy(kSignBitPayload.begin(), kSignBitPayload.end(), alloc->wdata().begin());
                (void)alloc->shrink(kSignBitPayload.size());
                callbacks.on_event_update(*cc_ptr, event_id, std::move(*alloc));
            }
            return score::Result<void>{};
        });

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);

    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available,
                                      MakeServerDefinition(config));
    prepared.peer_handlers->at(0)(true);

    EXPECT_EQ(sent_sample_ptr, sample_buffer.data());
}

// ============================================================================
// 4. Multi-event zero-copy allocation, wire prefix, and cross-event isolation
// ============================================================================

TEST_F(ProviderServiceBindingTest, AllocateRejectsOutOfRangeEventAndAllocationFailures) {
    auto const config = MakeTwoEventProviderConfig();
    auto prepared = ExpectPreparedSkeleton(config, {kTwoEvent0SampleSize, kTwoEvent1SampleSize});
    ASSERT_EQ(prepared.events.size(), 2U);

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    EXPECT_CALL(*prepared.events[0], Allocate(_))
        .WillOnce(
            Return(score::MakeUnexpected(score::mw::com::impl::ComErrc::kSampleAllocationFailure)))
        .WillOnce([](score::mw::com::impl::SampleAllocateeGuard guard) {
            return score::testing::MakeAllocatedSample(nullptr, std::move(guard));
        });
    EXPECT_CALL(*prepared.events[1], Allocate(_)).Times(0);

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available,
                                      MakeServerDefinition(config));

    // 1. Out-of-range event_id == config.events.size() (2).
    auto unknown_res = callbacks.on_event_payload_allocate(*cc_ptr, kOutOfRangeTwoEventId);
    ASSERT_FALSE(unknown_res.has_value());
    EXPECT_EQ(unknown_res.error(), Mw_com_binding_error::logic_error_unknown_event);

    // 2. Allocate() returns ComErrc::kSampleAllocationFailure.
    auto alloc_err_res = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
    ASSERT_FALSE(alloc_err_res.has_value());
    EXPECT_EQ(alloc_err_res.error(), Mw_com_binding_error::runtime_error_sample_allocation_failed);

    // 3. Allocate() returns a null sample pointer.
    auto null_ptr_res = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
    ASSERT_FALSE(null_ptr_res.has_value());
    EXPECT_EQ(null_ptr_res.error(), Mw_com_binding_error::runtime_error_sample_allocation_failed);
}

TEST_F(ProviderServiceBindingTest,
       AllocateExposesZeroCopyPayloadAtPrefixOffsetWithExactMaxPayloadSize) {
    auto const config = MakeTwoEventProviderConfig();
    auto prepared = ExpectPreparedSkeleton(config, {kTwoEvent0SampleSize, kTwoEvent1SampleSize});
    ASSERT_EQ(prepared.events.size(), 2U);

    alignas(kSample_alignment) std::array<std::byte, kTwoEvent0SampleSize> sample0{};
    alignas(kSample_alignment) std::array<std::byte, kTwoEvent1SampleSize> sample1{};

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    EXPECT_CALL(*prepared.events[0], Allocate(_))
        .WillOnce([&sample0](score::mw::com::impl::SampleAllocateeGuard guard) {
            return score::testing::MakeAllocatedSample(sample0.data(), std::move(guard));
        });
    EXPECT_CALL(*prepared.events[1], Allocate(_))
        .WillOnce([&sample1](score::mw::com::impl::SampleAllocateeGuard guard) {
            return score::testing::MakeAllocatedSample(sample1.data(), std::move(guard));
        });

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available,
                                      MakeServerDefinition(config));

    // Event 0: sample_size = 32, addressed = 28 -> wdata starts at sample0 + 8 with exact size 20
    // (excluding the 4 alignment-padding bytes [28..31]).
    auto payload0_res = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
    ASSERT_TRUE(payload0_res.has_value());
    EXPECT_EQ(payload0_res->wdata().data(), sample0.data() + kSample_prefix_size);
    EXPECT_EQ(payload0_res->wdata().size(), kTwoEvent0MaxPayload);
    EXPECT_EQ(payload0_res->data().data(), sample0.data() + kSample_prefix_size);
    EXPECT_EQ(payload0_res->data().size(), kTwoEvent0MaxPayload);
    EXPECT_FALSE(payload0_res->shrink(kTwoEvent0OversizedPayload));
    EXPECT_EQ(payload0_res->data().size(), kTwoEvent0MaxPayload);

    // Event 1: sample_size = 16, addressed = 12 -> wdata starts at sample1 + 8 with exact size 4
    // (excluding the 4 alignment-padding bytes [12..15]).
    auto payload1_res = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent1);
    ASSERT_TRUE(payload1_res.has_value());
    EXPECT_EQ(payload1_res->wdata().data(), sample1.data() + kSample_prefix_size);
    EXPECT_EQ(payload1_res->wdata().size(), kTwoEvent1MaxPayload);
    EXPECT_NE(payload1_res->wdata().data(), payload0_res->wdata().data());
}

TEST_F(ProviderServiceBindingTest,
       EventUpdateWritesHostLengthZeroesReservedBytesAndPreservesSignBitPayload) {
    auto const config = MakeTwoEventProviderConfig();
    auto prepared = ExpectPreparedSkeleton(config, {kTwoEvent0SampleSize, kTwoEvent1SampleSize});
    ASSERT_EQ(prepared.events.size(), 2U);

    alignas(kSample_alignment) std::array<std::byte, kTwoEvent0SampleSize> sample0_signbit{};
    sample0_signbit.fill(kGarbageFillByte);

    alignas(kSample_alignment) std::array<std::byte, kTwoEvent0SampleSize> sample0_full{};
    sample0_full.fill(kFullFillByte);

    alignas(kSample_alignment) std::array<std::byte, kTwoEvent1SampleSize> sample1_empty{};
    sample1_empty.fill(kAltFillByte);

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    EXPECT_CALL(*prepared.events[0], Allocate(_))
        .WillOnce([&sample0_signbit](score::mw::com::impl::SampleAllocateeGuard guard) {
            return score::testing::MakeAllocatedSample(sample0_signbit.data(), std::move(guard));
        })
        .WillOnce([&sample0_full](score::mw::com::impl::SampleAllocateeGuard guard) {
            return score::testing::MakeAllocatedSample(sample0_full.data(), std::move(guard));
        });
    EXPECT_CALL(*prepared.events[1], Allocate(_))
        .WillOnce([&sample1_empty](score::mw::com::impl::SampleAllocateeGuard guard) {
            return score::testing::MakeAllocatedSample(sample1_empty.data(), std::move(guard));
        });

    std::vector<void const*> event0_sent_ptrs{};
    EXPECT_CALL(*prepared.events[0], Send(_, _))
        .Times(2)
        .WillRepeatedly(
            [&event0_sent_ptrs](
                score::mw::com::impl::SampleAllocateePtr<void> sample,
                std::optional<
                    score::mw::com::impl::GenericSkeletonEventBinding::SendTraceCallback>) {
                event0_sent_ptrs.push_back(sample.Get());
                return score::Result<void>{};
            });

    void const* event1_sent_ptr{nullptr};
    EXPECT_CALL(*prepared.events[1], Send(_, _))
        .WillOnce([&event1_sent_ptr](
                      score::mw::com::impl::SampleAllocateePtr<void> sample,
                      std::optional<
                          score::mw::com::impl::GenericSkeletonEventBinding::SendTraceCallback>) {
            event1_sent_ptr = sample.Get();
            return score::Result<void>{};
        });

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available,
                                      MakeServerDefinition(config));

    // 1. Pre-filled 0xDD sample on event 0 with 4-byte sign-bit payload {0x00, 0x7F, 0x80, 0xFF}.
    auto signbit_alloc = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
    ASSERT_TRUE(signbit_alloc.has_value());
    std::copy(kSignBitPayload.begin(), kSignBitPayload.end(), signbit_alloc->wdata().begin());
    ASSERT_TRUE(signbit_alloc->shrink(kSignBitPayload.size()));
    callbacks.on_event_update(*cc_ptr, kSocomEvent0, std::move(*signbit_alloc));

    EXPECT_THAT(score::cpp::span<std::byte const>(sample0_signbit.data(), kPayloadLengthFieldSize),
                ElementsAreArray(kExpectedLength4Prefix));
    EXPECT_THAT(score::cpp::span<std::byte const>(sample0_signbit.data() + kReservedPrefixOffset,
                                                  kReservedPrefixSize),
                ElementsAreArray(kExpectedReservedZeros));
    EXPECT_THAT(score::cpp::span<std::byte const>(sample0_signbit.data() + kSample_prefix_size,
                                                  kSignBitVectorLength),
                ElementsAreArray(kSignBitPayload));
    score::cpp::span<std::byte const> const signbit_tail{
        sample0_signbit.data() + kSample_prefix_size + kSignBitPayload.size(),
        kTwoEvent0SampleSize - (kSample_prefix_size + kSignBitPayload.size())};
    ASSERT_EQ(signbit_tail.size(), kSignBitTailSize);
    EXPECT_THAT(signbit_tail, Each(kGarbageFillByte));

    // 2. Exact max_payload_size (20 bytes) on event 0 leaves alignment padding [28..31] untouched.
    auto full_alloc = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
    ASSERT_TRUE(full_alloc.has_value());
    std::copy(kPinned20BytePayload.begin(), kPinned20BytePayload.end(),
              full_alloc->wdata().begin());
    callbacks.on_event_update(*cc_ptr, kSocomEvent0, std::move(*full_alloc));

    EXPECT_THAT(score::cpp::span<std::byte const>(sample0_full.data(), kPayloadLengthFieldSize),
                ElementsAreArray(kExpectedLength20Prefix));
    EXPECT_THAT(score::cpp::span<std::byte const>(sample0_full.data() + kReservedPrefixOffset,
                                                  kReservedPrefixSize),
                ElementsAreArray(kExpectedReservedZeros));
    EXPECT_THAT(score::cpp::span<std::byte const>(sample0_full.data() + kSample_prefix_size,
                                                  kTwoEvent0MaxPayload),
                ElementsAreArray(kPinned20BytePayload));
    score::cpp::span<std::byte const> const full_padding{
        sample0_full.data() + kSample_prefix_size + kTwoEvent0MaxPayload,
        kTwoEvent0SampleSize - (kSample_prefix_size + kTwoEvent0MaxPayload)};
    ASSERT_EQ(full_padding.size(), kTwoEvent0AlignmentPaddingSize);
    EXPECT_THAT(full_padding, Each(kFullFillByte));

    // 3. Zero-byte payload boundary on event 1 writes 0 length and zeroes reserved bytes [4..7].
    auto empty_alloc = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent1);
    ASSERT_TRUE(empty_alloc.has_value());
    ASSERT_TRUE(empty_alloc->shrink(kZeroPayloadCount));
    callbacks.on_event_update(*cc_ptr, kSocomEvent1, std::move(*empty_alloc));

    EXPECT_THAT(score::cpp::span<std::byte const>(sample1_empty.data(), kPayloadLengthFieldSize),
                ElementsAreArray(kExpectedLength0Prefix));
    EXPECT_THAT(score::cpp::span<std::byte const>(sample1_empty.data() + kReservedPrefixOffset,
                                                  kReservedPrefixSize),
                ElementsAreArray(kExpectedReservedZeros));
    score::cpp::span<std::byte const> const empty_tail{sample1_empty.data() + kSample_prefix_size,
                                                       kTwoEvent1SampleSize - kSample_prefix_size};
    ASSERT_EQ(empty_tail.size(), kTwoEvent1EmptyTailSize);
    EXPECT_THAT(empty_tail, Each(kAltFillByte));

    ASSERT_EQ(event0_sent_ptrs.size(), 2U);
    EXPECT_EQ(event0_sent_ptrs[0], sample0_signbit.data());
    EXPECT_EQ(event0_sent_ptrs[1], sample0_full.data());
    EXPECT_EQ(event1_sent_ptr, sample1_empty.data());
}

TEST_F(ProviderServiceBindingTest, EventUpdateDropsCrossEventPayloads) {
    auto const config = MakeTwoEventProviderConfig();
    auto prepared = ExpectPreparedSkeleton(config, {kTwoEvent0SampleSize, kTwoEvent1SampleSize});
    ASSERT_EQ(prepared.events.size(), 2U);

    bool cross_0_to_1_released{false};
    TrackedSampleBuffer<kTwoEvent0SampleSize> cross_0_to_1_buf{{}, &cross_0_to_1_released, nullptr};

    bool cross_1_to_0_released{false};
    TrackedSampleBuffer<kTwoEvent1SampleSize> cross_1_to_0_buf{{}, &cross_1_to_0_released, nullptr};

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    EXPECT_CALL(*prepared.events[0], Allocate(_))
        .WillOnce([&cross_0_to_1_buf](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeTrackedAllocatedSample(cross_0_to_1_buf, std::move(guard));
        });
    EXPECT_CALL(*prepared.events[1], Allocate(_))
        .WillOnce([&cross_1_to_0_buf](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeTrackedAllocatedSample(cross_1_to_0_buf, std::move(guard));
        });

    EXPECT_CALL(*prepared.events[0], Send(_, _)).Times(0);
    EXPECT_CALL(*prepared.events[1], Send(_, _)).Times(0);

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available,
                                      MakeServerDefinition(config));

    // 1. Cross-event payload confusion (allocated on event 0, shrunk to 4 B <= event 1's max 4 B,
    //    then submitted to event 1): MUST be rejected without calling Send() on either event.
    {
        auto alloc0 = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
        ASSERT_TRUE(alloc0.has_value());
        ASSERT_TRUE(alloc0->shrink(kTwoEvent1MaxPayload));
        EXPECT_FALSE(cross_0_to_1_released);
        callbacks.on_event_update(*cc_ptr, kSocomEvent1, std::move(*alloc0));
        EXPECT_TRUE(cross_0_to_1_released);
    }

    // 2. Reverse cross-event payload confusion (allocated on event 1, submitted to event 0).
    {
        auto alloc1 = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent1);
        ASSERT_TRUE(alloc1.has_value());
        EXPECT_FALSE(cross_1_to_0_released);
        callbacks.on_event_update(*cc_ptr, kSocomEvent0, std::move(*alloc1));
        EXPECT_TRUE(cross_1_to_0_released);
    }
}

TEST_F(ProviderServiceBindingTest, EventUpdateDropsUnallocatedForeignAndOutOfRangeEventPayloads) {
    auto const config = MakeTwoEventProviderConfig();
    auto foreign_config = MakeTwoEventProviderConfig();
    foreign_config.instance_specifier = std::string{kSecondBridgedSpecifier};
    foreign_config.service_id = kSecondServiceId;

    auto prepared = ExpectPreparedSkeleton(config, {kTwoEvent0SampleSize, kTwoEvent1SampleSize});
    auto foreign_prepared =
        ExpectPreparedSkeleton(foreign_config, {kTwoEvent0SampleSize, kTwoEvent1SampleSize});
    ASSERT_EQ(prepared.events.size(), 2U);
    ASSERT_EQ(foreign_prepared.events.size(), 2U);

    bool oob_event_released{false};
    TrackedSampleBuffer<kTwoEvent0SampleSize> oob_event_buf{{}, &oob_event_released, nullptr};

    bool foreign_event_released{false};
    TrackedSampleBuffer<kTwoEvent0SampleSize> foreign_event_buf{
        {}, &foreign_event_released, nullptr};

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);
    socom::Client_connector::Callbacks foreign_callbacks{};
    auto* const foreign_cc_ptr = ExpectClientConnectorCreated(&foreign_callbacks);

    EXPECT_CALL(*prepared.events[0], Allocate(_))
        .WillOnce([&oob_event_buf](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeTrackedAllocatedSample(oob_event_buf, std::move(guard));
        });
    EXPECT_CALL(*prepared.events[1], Allocate(_)).Times(0);
    EXPECT_CALL(*prepared.events[0], Send(_, _)).Times(0);
    EXPECT_CALL(*prepared.events[1], Send(_, _)).Times(0);

    EXPECT_CALL(*foreign_prepared.events[0], Allocate(_))
        .WillOnce([&foreign_event_buf](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeTrackedAllocatedSample(foreign_event_buf, std::move(guard));
        });
    EXPECT_CALL(*foreign_prepared.events[1], Allocate(_)).Times(0);
    EXPECT_CALL(*foreign_prepared.events[0], Send(_, _)).Times(0);
    EXPECT_CALL(*foreign_prepared.events[1], Send(_, _)).Times(0);

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    auto foreign_binding_res = Provider_service_binding::create(socom_runtime_, foreign_config);
    ASSERT_TRUE(foreign_binding_res.has_value());
    auto foreign_binding = std::move(*foreign_binding_res);

    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available,
                                      MakeServerDefinition(config));
    foreign_callbacks.on_service_state_change(*foreign_cc_ptr, socom::Service_state::available,
                                              MakeServerDefinition(foreign_config));

    // 1. Out-of-range event_id on on_event_update drops and releases the allocated slot.
    {
        auto alloc_oob = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
        ASSERT_TRUE(alloc_oob.has_value());
        callbacks.on_event_update(*cc_ptr, kOutOfRangeTwoEventId, std::move(*alloc_oob));
    }
    EXPECT_TRUE(oob_event_released);

    // 2. Unallocated empty_payload() and unallocated span payload without Allocated_sample drop.
    callbacks.on_event_update(*cc_ptr, kSocomEvent0, socom::empty_payload());
    callbacks.on_event_update(*cc_ptr, kSocomEvent0, socom::Payload{kSignBitPayload});

    // 3. Foreign-binding payload (matching event_id, epoch, and size, but different owner) drops
    //    without calling Send() on either binding and releases its sample slot.
    {
        auto foreign_alloc =
            foreign_callbacks.on_event_payload_allocate(*foreign_cc_ptr, kSocomEvent0);
        ASSERT_TRUE(foreign_alloc.has_value());
        EXPECT_FALSE(foreign_event_released);
        callbacks.on_event_update(*cc_ptr, kSocomEvent0, std::move(*foreign_alloc));
        EXPECT_TRUE(foreign_event_released);
    }
}

TEST_F(ProviderServiceBindingTest, EventUpdateDropsPayloadExceedingMaxPayloadSize) {
    auto const config = MakeTwoEventProviderConfig();
    auto prepared = ExpectPreparedSkeleton(config, {kTwoEvent0SampleSize, kTwoEvent1SampleSize});
    ASSERT_EQ(prepared.events.size(), 2U);

    bool bounded_alloc_released{false};
    TrackedSampleBuffer<kTwoEvent0SampleSize> bounded_alloc_buf{
        {}, &bounded_alloc_released, nullptr};

    bool spoofed_oversized_released{false};
    TrackedSampleBuffer<kTwoEvent0SampleSize> spoofed_oversized_buf{
        {}, &spoofed_oversized_released, nullptr};

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    EXPECT_CALL(*prepared.events[0], Allocate(_))
        .WillOnce([&bounded_alloc_buf](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeTrackedAllocatedSample(bounded_alloc_buf, std::move(guard));
        });
    EXPECT_CALL(*prepared.events[1], Allocate(_)).Times(0);
    EXPECT_CALL(*prepared.events[0], Send(_, _)).Times(0);
    EXPECT_CALL(*prepared.events[1], Send(_, _)).Times(0);

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available,
                                      MakeServerDefinition(config));

    // 1. Allocated Writable_payload is capped at max_payload_size (20 B) and refuses to expand into
    //    the 4-byte alignment padding [28..31].
    {
        auto alloc_bounded = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
        ASSERT_TRUE(alloc_bounded.has_value());
        EXPECT_FALSE(alloc_bounded->shrink(kTwoEvent0OversizedPayload));
        EXPECT_EQ(alloc_bounded->data().size(), kTwoEvent0MaxPayload);
    }
    EXPECT_TRUE(bounded_alloc_released);

    // 2. Spoofed Payload carrying a raw SampleAllocateePtr<void> and a 21-byte span
    //    (max_payload_size + 1) is rejected without calling Send() and releases the sample slot.
    std::array<std::byte, kTwoEvent0OversizedPayload> oversized_bytes{};
    oversized_bytes.fill(kOversizedFillByte);
    auto spoofed_sample = MakeTrackedAllocatedSample(spoofed_oversized_buf, {});
    ASSERT_TRUE(spoofed_sample.has_value());
    EXPECT_FALSE(spoofed_oversized_released);
    callbacks.on_event_update(*cc_ptr, kSocomEvent0,
                              socom::Payload{oversized_bytes, std::move(*spoofed_sample)});
    EXPECT_TRUE(spoofed_oversized_released);
}

TEST_F(ProviderServiceBindingTest,
       AbandoningOrMovingAllocatedPayloadTransfersAndReleasesSampleSlotExactlyOnce) {
    auto const config = MakeTwoEventProviderConfig();
    auto prepared = ExpectPreparedSkeleton(config, {kTwoEvent0SampleSize, kTwoEvent1SampleSize});
    ASSERT_EQ(prepared.events.size(), 2U);

    bool abandoned_released{false};
    TrackedSampleBuffer<kTwoEvent0SampleSize> buf_abandoned{{}, &abandoned_released, nullptr};

    bool moved_released{false};
    std::vector<std::string> moved_release_trace{};
    TrackedSampleBuffer<kTwoEvent0SampleSize> buf_moved{{}, &moved_released, &moved_release_trace};

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    EXPECT_CALL(*prepared.events[0], Allocate(_))
        .WillOnce([&buf_abandoned](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeTrackedAllocatedSample(buf_abandoned, std::move(guard));
        })
        .WillOnce([&buf_moved](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeTrackedAllocatedSample(buf_moved, std::move(guard));
        });
    EXPECT_CALL(*prepared.events[0], Send(_, _)).Times(0);
    EXPECT_CALL(*prepared.events[1], Send(_, _)).Times(0);

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available,
                                      MakeServerDefinition(config));

    // 1. Abandoning Writable_payload without sending immediately returns the slot to LoLa.
    {
        auto abandoned = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
        ASSERT_TRUE(abandoned.has_value());
        EXPECT_FALSE(abandoned_released);
    }
    EXPECT_TRUE(abandoned_released);

    // 2. Moving Writable_payload into Payload transfers inline slot ownership without premature
    //    release on source destruction, and releases the slot once when Payload is destroyed.
    {
        std::optional<socom::Payload> moved_into_payload{};
        {
            auto source = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
            ASSERT_TRUE(source.has_value());
            moved_into_payload.emplace(std::move(*source));
            EXPECT_FALSE(moved_released);
        }
        EXPECT_FALSE(moved_released);
        EXPECT_TRUE(moved_release_trace.empty());

        moved_into_payload.reset();
        EXPECT_TRUE(moved_released);
        ASSERT_EQ(moved_release_trace.size(), 1U);
        EXPECT_EQ(moved_release_trace[0], "allocation_released");
    }
}

// ============================================================================
// 5. Destruction teardown order & multithreaded synchronization
// ============================================================================

TEST_F(ProviderServiceBindingTest, DestructorTearsDownHandlersConnectorAndActiveOfferInOrder) {
    auto const config = MakeServiceConfig(Role::provider);
    auto prepared = ExpectPreparedSkeleton(config, {kDefaultEvent0SampleSize});
    ASSERT_EQ(prepared.events.size(), 1U);

    std::vector<std::string> teardown_trace{};

    socom::Client_connector::Callbacks callbacks{};
    auto cc_mock = std::make_unique<DestructorTrackingClientConnectorMock>(&teardown_trace);
    auto* const cc_ptr = cc_mock.get();
    EXPECT_CALL(socom_runtime_, make_client_connector(_, _, _))
        .InSequence(client_connector_sequence())
        .WillOnce([cc = std::move(cc_mock), &callbacks](
                      socom::Service_interface_definition, socom::Service_instance,
                      socom::Client_connector::Callbacks cb) mutable
                      -> score::Result<socom::Client_connector::Uptr> {
            callbacks = std::move(cb);
            return std::move(cc);
        })
        .RetiresOnSaturation();

    EXPECT_CALL(*prepared.events[0], UnsetReceiveHandlerRegistrationChangedHandler())
        .WillOnce([&teardown_trace]() noexcept {
            teardown_trace.emplace_back("handler_unset");
            return score::Result<void>{};
        });
    EXPECT_CALL(*prepared.skeleton, PrepareStopOffer(_))
        .WillOnce(
            [&teardown_trace](
                std::optional<
                    score::mw::com::impl::SkeletonBinding::UnregisterShmObjectTraceCallback>) {
                teardown_trace.emplace_back("stop_offer");
            });

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);

    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available,
                                      MakeServerDefinition(config));
    EXPECT_TRUE(teardown_trace.empty());

    binding.reset();

    ASSERT_EQ(teardown_trace.size(), 3U);
    EXPECT_THAT(teardown_trace, ElementsAre("handler_unset", "connector_destroyed", "stop_offer"));
}

TEST_F(ProviderServiceBindingTest,
       NotAvailableAndReofferRejectStaleEpochPayloadsAcrossRecreatedAndRetainedSkeletons) {
    auto const config = MakeServiceConfig(Role::provider);
    auto first_skeleton = ExpectPreparedSkeleton(config, {kDefaultEvent0SampleSize});
    auto second_skeleton = ExpectPreparedSkeleton(config, {kDefaultEvent0SampleSize});
    ASSERT_EQ(first_skeleton.events.size(), 1U);
    ASSERT_EQ(second_skeleton.events.size(), 1U);

    bool first_abandoned_released{false};
    TrackedSampleBuffer<kDefaultEvent0SampleSize> first_abandoned_buf{
        {}, &first_abandoned_released, nullptr};

    bool stale_unavail_released{false};
    TrackedSampleBuffer<kDefaultEvent0SampleSize> stale_unavail_buf{
        {}, &stale_unavail_released, nullptr};

    bool stale_retained_released{false};
    TrackedSampleBuffer<kDefaultEvent0SampleSize> stale_retained_buf{
        {}, &stale_retained_released, nullptr};

    bool fresh_valid_released{false};
    TrackedSampleBuffer<kDefaultEvent0SampleSize> fresh_valid_buf{
        {}, &fresh_valid_released, nullptr};

    socom::Client_connector::Callbacks callbacks{};
    auto* const cc_ptr = ExpectClientConnectorCreated(&callbacks);

    EXPECT_CALL(*first_skeleton.events[0], Allocate(_))
        .WillOnce([&first_abandoned_buf](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeTrackedAllocatedSample(first_abandoned_buf, std::move(guard));
        });
    EXPECT_CALL(*first_skeleton.events[0], Send(_, _)).Times(0);

    EXPECT_CALL(*second_skeleton.events[0], Allocate(_))
        .WillOnce([&stale_unavail_buf](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeTrackedAllocatedSample(stale_unavail_buf, std::move(guard));
        })
        .WillOnce([&stale_retained_buf](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeTrackedAllocatedSample(stale_retained_buf, std::move(guard));
        })
        .WillOnce([&fresh_valid_buf](score::mw::com::impl::SampleAllocateeGuard guard) {
            return MakeTrackedAllocatedSample(fresh_valid_buf, std::move(guard));
        });

    void const* sent_sample_ptr{nullptr};
    EXPECT_CALL(*second_skeleton.events[0], Send(_, _))
        .WillOnce([&sent_sample_ptr](
                      score::mw::com::impl::SampleAllocateePtr<void> sample,
                      std::optional<
                          score::mw::com::impl::GenericSkeletonEventBinding::SendTraceCallback>) {
            sent_sample_ptr = sample.Get();
            return score::Result<void>{};
        });

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    auto const server_def = MakeServerDefinition(config);

    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);

    // 1. Producer releases in-flight Writable_payload before disabling its SOCom server connector;
    //    with wanted_by_peer == false, not_available destroys first_skeleton cleanly, and
    //    subsequent allocate / update while unavailable fail / drop safely.
    {
        auto first_alloc = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
        ASSERT_TRUE(first_alloc.has_value());
        EXPECT_FALSE(first_abandoned_released);
    }
    EXPECT_TRUE(first_abandoned_released);

    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::not_available, server_def);
    auto unavail_alloc = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
    ASSERT_FALSE(unavail_alloc.has_value());
    EXPECT_EQ(unavail_alloc.error(), Mw_com_binding_error::runtime_error_sample_allocation_failed);
    callbacks.on_event_update(*cc_ptr, kSocomEvent0, socom::Payload{kSignBitPayload});

    // 2. Recreating second_skeleton on available offers under a new epoch. With wanted_by_peer ==
    //    true, not_available retains second_skeleton; submitting a payload while unavailable or
    //    after re-offering under the next epoch rejects both stale payloads and releases their
    //    slots.
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);
    auto stale_unavail = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
    auto stale_retained = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
    ASSERT_TRUE(stale_unavail.has_value());
    ASSERT_TRUE(stale_retained.has_value());

    ASSERT_FALSE(second_skeleton.peer_handlers->at(0).empty());
    second_skeleton.peer_handlers->at(0)(true);
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::not_available, server_def);

    EXPECT_FALSE(stale_unavail_released);
    callbacks.on_event_update(*cc_ptr, kSocomEvent0, std::move(*stale_unavail));
    EXPECT_TRUE(stale_unavail_released);

    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);
    EXPECT_FALSE(stale_retained_released);
    callbacks.on_event_update(*cc_ptr, kSocomEvent0, std::move(*stale_retained));
    EXPECT_TRUE(stale_retained_released);
    EXPECT_EQ(sent_sample_ptr, nullptr);

    // 3. Fresh allocation under the active offer epoch succeeds (valid -> stale rejected -> valid).
    auto fresh_valid = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
    ASSERT_TRUE(fresh_valid.has_value());
    ASSERT_TRUE(fresh_valid->shrink(kSignBitPayload.size()));
    callbacks.on_event_update(*cc_ptr, kSocomEvent0, std::move(*fresh_valid));
    EXPECT_EQ(sent_sample_ptr, fresh_valid_buf.bytes.data());
    EXPECT_TRUE(fresh_valid_released);
}

TEST_F(ProviderServiceBindingTest, ConcurrentStateInterestAndPayloadOperationsSynchronizeSafely) {
    auto const config = MakeTwoEventProviderConfig();
    StubProviderEventsResolved(config);

    // Declared before binding and worker threads so backing sample memory outlives all workers
    // even when not_available clears parked SampleAllocateePtrs mid-copy.
    auto sample_arena = std::make_shared<ConcurrentSampleArena>();
    auto callback_mutex = std::make_shared<std::mutex>();
    auto shared_peer_callback0 =
        std::make_shared<score::mw::com::impl::ReceiveHandlerRegistrationChangedCallback>();
    auto shared_peer_callback1 =
        std::make_shared<score::mw::com::impl::ReceiveHandlerRegistrationChangedCallback>();
    std::atomic<std::size_t> sent_count{0U};

    ON_CALL(skeleton_binding_factory_mock(), Create(_))
        .WillByDefault([](score::mw::com::impl::InstanceIdentifier const&) noexcept {
            return std::make_unique<ConcurrentFakeSkeleton>();
        });
    ON_CALL(generic_skeleton_event_factory_mock(), Create(_, _, kEvent0, _))
        .WillByDefault([sample_arena, callback_mutex, shared_peer_callback0, &sent_count](
                           score::mw::com::impl::InstanceIdentifier const&,
                           score::mw::com::impl::SkeletonBinding&, std::string_view,
                           score::memory::DataTypeSizeInfo) {
            return std::make_unique<ConcurrentFakeSkeletonEvent>(
                kTwoEvent0SampleSize, sample_arena, callback_mutex, shared_peer_callback0,
                &sent_count);
        });
    ON_CALL(generic_skeleton_event_factory_mock(), Create(_, _, kEvent1, _))
        .WillByDefault([sample_arena, callback_mutex, shared_peer_callback1, &sent_count](
                           score::mw::com::impl::InstanceIdentifier const&,
                           score::mw::com::impl::SkeletonBinding&, std::string_view,
                           score::memory::DataTypeSizeInfo) {
            return std::make_unique<ConcurrentFakeSkeletonEvent>(
                kTwoEvent1SampleSize, sample_arena, callback_mutex, shared_peer_callback1,
                &sent_count);
        });

    socom::Client_connector::Callbacks callbacks{};
    socom::Client_connector* cc_ptr{nullptr};
    EXPECT_CALL(socom_runtime_, make_client_connector(_, _, _))
        .InSequence(client_connector_sequence())
        .WillOnce([&callbacks, &cc_ptr](socom::Service_interface_definition definition,
                                        socom::Service_instance instance,
                                        socom::Client_connector::Callbacks cb)
                      -> score::Result<socom::Client_connector::Uptr> {
            auto fake_cc = std::make_unique<ConcurrentFakeClientConnector>(std::move(definition),
                                                                           std::move(instance));
            cc_ptr = fake_cc.get();
            callbacks = std::move(cb);
            return fake_cc;
        })
        .RetiresOnSaturation();

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    ASSERT_NE(cc_ptr, nullptr);
    auto const server_def = MakeServerDefinition(config);

    PromiseGate start_gate{};
    PromiseGate all_ready_gate{};
    std::atomic<std::size_t> ready_workers{0U};
    std::atomic<bool> start_gate_timed_out{false};

    auto wait_for_start = [&ready_workers, &all_ready_gate,
                           &start_gate_timed_out](std::shared_future<void> start_future) -> bool {
        if (ready_workers.fetch_add(1U, std::memory_order_acq_rel) ==
            (kConcurrentWorkerCount - 1U)) {
            all_ready_gate.Signal();
        }
        if (start_future.wait_for(kConcurrencyTimeout) != std::future_status::ready) {
            start_gate_timed_out.store(true, std::memory_order_release);
            return false;
        }
        return true;
    };

    // Models SOCom's Enabled_server_connector::disable() contract: local producers complete or
    // drop in-flight Writable_payloads before disable() delivers Service_state::not_available,
    // because GenericSkeleton::~GenericSkeleton() aborts if a SampleAllocateePtr outlives it.
    std::shared_mutex producer_session_mutex{};

    ScopedThread state_worker{};
    ScopedThread interest_worker{};
    ScopedThread payload_worker{};
    ScopedGateSignaler const unblock_workers_on_exit{start_gate};

    state_worker = ScopedThread{std::thread{[&callbacks, cc_ptr, &server_def,
                                             &producer_session_mutex, &wait_for_start,
                                             start_future = start_gate.Future()]() {
        if (!wait_for_start(start_future)) {
            return;
        }
        for (std::size_t iter = 0U; iter < kConcurrentIterations; ++iter) {
            callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);
            std::this_thread::yield();
            {
                std::unique_lock<std::shared_mutex> const producer_lock{producer_session_mutex};
                callbacks.on_service_state_change(*cc_ptr, socom::Service_state::not_available,
                                                  server_def);
            }
            std::this_thread::yield();
        }
    }}};

    interest_worker =
        ScopedThread{std::thread{[callback_mutex, shared_peer_callback0, shared_peer_callback1,
                                  &wait_for_start, start_future = start_gate.Future()]() {
            if (!wait_for_start(start_future)) {
                return;
            }
            for (std::size_t iter = 0U; iter < kConcurrentIterations; ++iter) {
                bool const wanted = (iter < (kConcurrentIterations / 2U)) && ((iter % 2U) == 0U);
                {
                    std::lock_guard<std::mutex> const lock{*callback_mutex};
                    if (!shared_peer_callback0->empty()) {
                        (*shared_peer_callback0)(wanted);
                    }
                    if (!shared_peer_callback1->empty()) {
                        (*shared_peer_callback1)(false);
                    }
                }
                std::this_thread::yield();
            }
        }}};

    payload_worker = ScopedThread{std::thread{[&callbacks, cc_ptr, &producer_session_mutex,
                                               &wait_for_start,
                                               start_future = start_gate.Future()]() {
        if (!wait_for_start(start_future)) {
            return;
        }
        for (std::size_t iter = 0U; iter < kConcurrentIterations; ++iter) {
            auto const target_event = ((iter % 2U) == 0U) ? kSocomEvent0 : kSocomEvent1;
            std::shared_lock<std::shared_mutex> const producer_lock{producer_session_mutex};
            auto alloc = callbacks.on_event_payload_allocate(*cc_ptr, target_event);
            if (alloc.has_value()) {
                std::copy(kSignBitPayload.begin(), kSignBitPayload.end(), alloc->wdata().begin());
                (void)alloc->shrink(kSignBitPayload.size());
                if ((iter % 4U) != 3U) {
                    callbacks.on_event_update(*cc_ptr, target_event, std::move(*alloc));
                }
            } else {
                callbacks.on_event_update(*cc_ptr, target_event, socom::Payload{kSignBitPayload});
            }
        }
    }}};

    ASSERT_TRUE(all_ready_gate.WaitFor(kConcurrencyTimeout));
    start_gate.Signal();

    state_worker.Join();
    interest_worker.Join();
    payload_worker.Join();
    ASSERT_FALSE(start_gate_timed_out.load(std::memory_order_acquire));

    // Final deterministic sanity check after concurrent storm: bring service available and send 1
    // event sample cleanly.
    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);
    auto final_alloc = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
    ASSERT_TRUE(final_alloc.has_value());
    ASSERT_TRUE(final_alloc->shrink(kZeroPayloadCount));
    auto const count_before_final = sent_count.load(std::memory_order_relaxed);
    callbacks.on_event_update(*cc_ptr, kSocomEvent0, std::move(*final_alloc));
    EXPECT_EQ(sent_count.load(std::memory_order_relaxed), count_before_final + 1U);

    binding.reset();
    EXPECT_EQ(sample_arena->active_slots(), 0U);
}

TEST_F(ProviderServiceBindingTest,
       ConcurrentNotAvailableWaitsForInFlightSendBeforeDestroyingSkeleton) {
    class BlockingSendSkeletonEvent final
        : public score::mw::com::impl::GenericSkeletonEventBinding {
       public:
        BlockingSendSkeletonEvent(std::size_t const sample_bytes,
                                  std::shared_ptr<ConcurrentSampleArena> sample_arena,
                                  PromiseGate* const entered_send,
                                  std::shared_future<void> release_send,
                                  std::atomic<bool>* const destroyed,
                                  std::atomic<bool>* const destroyed_during_send) noexcept
            : m_sample_bytes{sample_bytes},
              m_sample_arena{std::move(sample_arena)},
              m_entered_send{entered_send},
              m_release_send{std::move(release_send)},
              m_destroyed{destroyed},
              m_destroyed_during_send{destroyed_during_send} {}

        ~BlockingSendSkeletonEvent() noexcept override {
            if (m_destroyed != nullptr) {
                m_destroyed->store(true, std::memory_order_release);
            }
        }

        score::Result<void> Send(score::mw::com::impl::SampleAllocateePtr<void> sample,
                                 std::optional<SendTraceCallback>) override {
            (void)sample;
            auto release_send = m_release_send;
            auto* const destroyed = m_destroyed;
            auto* const destroyed_during_send = m_destroyed_during_send;
            auto* const entered_send = m_entered_send;
            if (entered_send != nullptr) {
                entered_send->Signal();
            }
            (void)release_send.wait_for(kInFlightSendOverlapWindow);
            if (destroyed != nullptr && destroyed_during_send != nullptr &&
                destroyed->load(std::memory_order_acquire)) {
                destroyed_during_send->store(true, std::memory_order_release);
            }
            return {};
        }

        score::Result<score::mw::com::impl::SampleAllocateePtr<void>> Allocate(
            score::mw::com::impl::SampleAllocateeGuard guard) override {
            return m_sample_arena->Allocate(m_sample_bytes, std::move(guard));
        }

        score::Result<score::mw::com::impl::SamplePtr<void>> GetLatestSample(
            score::mw::com::impl::QualityType) override {
            return score::MakeUnexpected(score::mw::com::impl::ComErrc::kBindingFailure);
        }

        score::Result<void> PrepareOffer(
            std::optional<score::mw::com::impl::InitializeSampleCallback> const&) override {
            return {};
        }

        void PrepareStopOffer() override {}

        score::memory::DataTypeSizeInfo GetEventDataTypeSizeInfo() const override {
            return score::memory::DataTypeSizeInfo{m_sample_bytes, kSample_alignment};
        }

        score::mw::com::impl::BindingType GetBindingType() const noexcept override {
            return score::mw::com::impl::BindingType::kLoLa;
        }

        void SetSkeletonEventTracingData(
            score::mw::com::impl::tracing::SkeletonEventTracingData) noexcept override {}

        score::Result<void> Notify() override { return {}; }

        score::Result<void> SetReceiveHandlerRegistrationChangedHandler(
            score::mw::com::impl::ReceiveHandlerRegistrationChangedCallback) noexcept override {
            return {};
        }

        score::Result<void> UnsetReceiveHandlerRegistrationChangedHandler() noexcept override {
            return {};
        }

       private:
        std::size_t m_sample_bytes;
        std::shared_ptr<ConcurrentSampleArena> m_sample_arena;
        PromiseGate* m_entered_send;
        std::shared_future<void> m_release_send;
        std::atomic<bool>* m_destroyed;
        std::atomic<bool>* m_destroyed_during_send;
    };

    auto const config = MakeServiceConfig(Role::provider);
    StubProviderEventsResolved(config);

    auto sample_arena = std::make_shared<ConcurrentSampleArena>();
    PromiseGate entered_send_gate{};
    PromiseGate release_send_gate{};
    std::atomic<bool> event_destroyed{false};
    std::atomic<bool> destroyed_during_send{false};

    EXPECT_CALL(skeleton_binding_factory_mock(), Create(_))
        .InSequence(skeleton_factory_sequence())
        .WillOnce([](score::mw::com::impl::InstanceIdentifier const&) noexcept {
            return std::make_unique<ConcurrentFakeSkeleton>();
        })
        .RetiresOnSaturation();
    EXPECT_CALL(generic_skeleton_event_factory_mock(), Create(_, _, kEvent0, _))
        .WillOnce([sample_arena, &entered_send_gate, release_future = release_send_gate.Future(),
                   &event_destroyed,
                   &destroyed_during_send](score::mw::com::impl::InstanceIdentifier const&,
                                           score::mw::com::impl::SkeletonBinding&, std::string_view,
                                           score::memory::DataTypeSizeInfo) {
            return std::make_unique<BlockingSendSkeletonEvent>(
                kDefaultEvent0SampleSize, sample_arena, &entered_send_gate, release_future,
                &event_destroyed, &destroyed_during_send);
        })
        .RetiresOnSaturation();

    socom::Client_connector::Callbacks callbacks{};
    socom::Client_connector* cc_ptr{nullptr};
    EXPECT_CALL(socom_runtime_, make_client_connector(_, _, _))
        .InSequence(client_connector_sequence())
        .WillOnce([&callbacks, &cc_ptr](socom::Service_interface_definition definition,
                                        socom::Service_instance instance,
                                        socom::Client_connector::Callbacks cb)
                      -> score::Result<socom::Client_connector::Uptr> {
            auto fake_cc = std::make_unique<ConcurrentFakeClientConnector>(std::move(definition),
                                                                           std::move(instance));
            cc_ptr = fake_cc.get();
            callbacks = std::move(cb);
            return fake_cc;
        })
        .RetiresOnSaturation();

    auto binding_res = Provider_service_binding::create(socom_runtime_, config);
    ASSERT_TRUE(binding_res.has_value());
    auto binding = std::move(*binding_res);
    ASSERT_NE(cc_ptr, nullptr);
    auto const server_def = MakeServerDefinition(config);

    callbacks.on_service_state_change(*cc_ptr, socom::Service_state::available, server_def);
    auto alloc = callbacks.on_event_payload_allocate(*cc_ptr, kSocomEvent0);
    ASSERT_TRUE(alloc.has_value());
    ASSERT_TRUE(alloc->shrink(kZeroPayloadCount));

    ScopedThread send_thread{};
    ScopedThread withdraw_thread{};
    ScopedGateSignaler const unblock_send_on_exit{release_send_gate};

    send_thread =
        ScopedThread{std::thread{[&callbacks, cc_ptr, payload = std::move(*alloc)]() mutable {
            callbacks.on_event_update(*cc_ptr, kSocomEvent0, std::move(payload));
        }}};

    ASSERT_TRUE(entered_send_gate.WaitFor(kConcurrencyTimeout));

    // Concurrent not_available (with wanted_by_peer == false) must wait for in-flight Send() and
    // its SampleAllocateePtr to finish before destroying m_skeleton.
    withdraw_thread = ScopedThread{std::thread{[&callbacks, cc_ptr, &server_def]() {
        callbacks.on_service_state_change(*cc_ptr, socom::Service_state::not_available, server_def);
    }}};

    send_thread.Join();
    withdraw_thread.Join();

    EXPECT_FALSE(destroyed_during_send.load(std::memory_order_acquire));
    EXPECT_TRUE(event_destroyed.load(std::memory_order_acquire));
    binding.reset();
    EXPECT_EQ(sample_arena->active_slots(), 0U);
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
