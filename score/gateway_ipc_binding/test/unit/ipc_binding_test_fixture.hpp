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

#ifndef SCORE_GATEWAY_IPC_BINDING_TEST_UNIT_IPC_BINDING_TEST_FIXTURE_HPP
#define SCORE_GATEWAY_IPC_BINDING_TEST_UNIT_IPC_BINDING_TEST_FIXTURE_HPP

#include <gmock/gmock.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "score/filesystem/path.h"
#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/gateway_ipc_binding/impl/someipd_service.hpp"
#include "score/mw/com/impl/handle_type.h"
#include "score/mw/com/impl/i_runtime.h"
#include "score/mw/com/impl/instance_identifier.h"
#include "score/mw/com/impl/instance_specifier.h"
#include "score/mw/com/impl/runtime.h"
#include "score/mw/com/runtime.h"
#include "score/mw/com/runtime_configuration.h"
#include "score/result/result.h"
#include "score/socom/service_interface_identifier.hpp"
#include "score/test/mw_com_mocks.hpp"
#include "score/test/mw_com_test_fixture.hpp"

namespace score::gateway_ipc_binding::mw_com {

inline constexpr std::string_view kDefaultServiceInterfacePath{"/score/test/Service"};
inline constexpr std::string_view kDefaultServiceTypeName{"test_type"};
inline constexpr std::string_view kDefaultServiceInstanceName{"1"};
inline constexpr std::uint16_t kDefaultServiceId{100U};
inline constexpr std::uint16_t kSecondServiceId{101U};
inline constexpr std::uint16_t kSomeipdServiceId{6400U};
inline constexpr std::uint16_t kInitialUniqueBridgedServiceId{20000U};
inline constexpr std::uint16_t kDefaultInstanceId{1U};
inline constexpr std::uint8_t kDefaultMajorVersion{1U};
inline constexpr std::uint16_t kDefaultMinorVersion{0U};

inline constexpr std::string_view kValidBridgedSpecifier{"bridged_ipc/service"};
inline constexpr std::string_view kSecondBridgedSpecifier{"bridged_ipc/second_service"};
inline constexpr std::string_view kSomeipdSpecifier{"someipd/daemon"};
inline constexpr std::string_view kSomeipdTypeName{"/score/someip/SomeipdService"};
inline constexpr std::string_view kInvalidSpecifier{"invalid//specifier"};
inline constexpr std::string_view kInvalidBridgedSpecifier{"bridged_//invalid"};

inline constexpr std::string_view kEvent0{"ev0"};
inline constexpr std::uint16_t kEvent0Id{1U};
inline constexpr std::size_t kEvent0MaxPayload{16U};

inline constexpr std::string_view kEvent1{"ev1"};
inline constexpr std::uint16_t kEvent1Id{2U};
inline constexpr std::size_t kEvent1MaxPayload{32U};

inline constexpr std::string_view kEvent2{"ev2"};
inline constexpr std::uint16_t kEvent2Id{3U};
inline constexpr std::size_t kEvent2MaxPayload{64U};

inline constexpr std::size_t kDefaultMaxSampleCount{4U};

inline Service_config MakeServiceConfig(Role const role = Role::consumer,
                                        std::string_view const specifier = kValidBridgedSpecifier) {
    socom::Service_interface_identifier const interface{
        kDefaultServiceInterfacePath,
        utils::string_registry::LiteralTag{},
        {kDefaultMajorVersion, kDefaultMinorVersion}};
    socom::Service_instance const instance{kDefaultServiceInstanceName,
                                           utils::string_registry::LiteralTag{}};
    return Service_config{
        interface,
        instance,
        std::string{specifier},
        kDefaultServiceId,
        kDefaultInstanceId,
        role,
        {Event_config{std::string{kEvent0}, Mw_com_event_id{kEvent0Id}, kEvent0MaxPayload}},
        kDefaultMaxSampleCount,
    };
}

inline Service_config MakeMultiEventServiceConfig(
    Role const role, std::vector<Event_config> events,
    std::string_view const specifier = kValidBridgedSpecifier,
    std::uint16_t const service_id = kDefaultServiceId,
    std::size_t const max_sample_count = kDefaultMaxSampleCount) {
    socom::Service_interface_identifier const interface{
        kDefaultServiceInterfacePath,
        utils::string_registry::LiteralTag{},
        {kDefaultMajorVersion, kDefaultMinorVersion}};
    socom::Service_instance const instance{kDefaultServiceInstanceName,
                                           utils::string_registry::LiteralTag{}};
    return Service_config{
        interface,          instance, std::string{specifier}, service_id,
        kDefaultInstanceId, role,     std::move(events),      max_sample_count,
    };
}

class IpcBindingTestFixture : public testing::MwComTestFixture {
   protected:
    using SomeipdCallBuffers = testing::TypedMethodCallBuffers<Service_configuration, bool>;
    using MockSomeipdProxyRpc = testing::MockTypedProxyWithMethod<Service_configuration, bool>;

    struct UniqueBridgedIds {
        std::string specifier;
        std::uint16_t service_id;
    };

    IpcBindingTestFixture() {
        EnsureRealRuntimeInitialized();
        ON_CALL(runtime_mock(), resolve(::testing::_))
            .WillByDefault([](score::mw::com::InstanceSpecifier const& spec) {
                return RealRuntime().resolve(spec);
            });
        StubInstanceResolved();
    }

    static UniqueBridgedIds NextUniqueBridgedIds(std::string_view const label = "svc") {
        static std::atomic<std::uint16_t> next_id{kInitialUniqueBridgedServiceId};
        auto const id = next_id.fetch_add(1U, std::memory_order_relaxed);
        return UniqueBridgedIds{
            "bridged_ut/" + std::string{label} + "_" + std::to_string(id),
            id,
        };
    }

    using MwComTestFixture::StubInstanceNotResolved;
    using MwComTestFixture::StubInstanceResolved;

    void StubInstanceResolved(std::initializer_list<std::string_view> const events = {kEvent0}) {
        StubInstanceResolved(kValidBridgedSpecifier, events, kDefaultServiceId, kDefaultInstanceId);
    }

    void StubInstanceResolved(std::string_view const specifier,
                              std::initializer_list<std::string_view> const events,
                              std::uint16_t const service_id = kDefaultServiceId,
                              std::uint16_t const instance_id = kDefaultInstanceId) {
        MwComTestFixture::StubInstanceResolved(
            specifier, events,
            testing::ServiceDeploymentSpec{kDefaultServiceTypeName, service_id,
                                           kDefaultMajorVersion, kDefaultMinorVersion,
                                           instance_id});
    }

    void StubSomeipdInstanceResolved(std::string_view const specifier = kSomeipdSpecifier) {
        MwComTestFixture::StubInstanceResolved(
            specifier, {},
            testing::ServiceDeploymentSpec{kSomeipdTypeName, kSomeipdServiceId,
                                           kDefaultMajorVersion, kDefaultMinorVersion,
                                           kDefaultInstanceId});
    }

    score::mw::com::impl::InstanceIdentifier MakeIdentifier(
        std::initializer_list<std::string_view> const events = {kEvent0}) {
        return MakeIdentifier(kValidBridgedSpecifier, events);
    }

    score::mw::com::impl::InstanceIdentifier MakeIdentifier(
        std::string_view const specifier,
        std::initializer_list<std::string_view> const events = {kEvent0},
        std::uint16_t const service_id = kDefaultServiceId,
        std::uint16_t const instance_id = kDefaultInstanceId) {
        return instance_holder().MakeIdentifier(
            specifier, events,
            testing::ServiceDeploymentSpec{kDefaultServiceTypeName, service_id,
                                           kDefaultMajorVersion, kDefaultMinorVersion,
                                           instance_id});
    }

    score::mw::com::impl::HandleType MakeHandle(
        std::initializer_list<std::string_view> const events = {kEvent0}) {
        return MakeHandle(kValidBridgedSpecifier, events);
    }

    score::mw::com::impl::HandleType MakeHandle(
        std::string_view const specifier,
        std::initializer_list<std::string_view> const events = {kEvent0},
        std::uint16_t const service_id = kDefaultServiceId,
        std::uint16_t const instance_id = kDefaultInstanceId) {
        return instance_holder().MakeHandle(
            specifier, events,
            testing::ServiceDeploymentSpec{kDefaultServiceTypeName, service_id,
                                           kDefaultMajorVersion, kDefaultMinorVersion,
                                           instance_id});
    }

    score::mw::com::impl::HandleType MakeSomeipdHandle(
        std::string_view const specifier = kSomeipdSpecifier,
        std::uint16_t const instance_id = kDefaultInstanceId) {
        return instance_holder().MakeHandle(
            specifier, {},
            testing::ServiceDeploymentSpec{kSomeipdTypeName, kSomeipdServiceId,
                                           kDefaultMajorVersion, kDefaultMinorVersion,
                                           instance_id});
    }

    SomeipdCallBuffers AllocateSomeipdCallBuffers() {
        return AllocateMethodCallBuffers<Service_configuration, bool>();
    }

    MockSomeipdProxyRpc ExpectSomeipdProxyWithRpc(
        std::optional<score::Result<bool>> const rpc_reply = score::Result<bool>{true}) {
        auto rpc =
            ExpectTypedProxyWithMethod<Service_configuration, bool>("AddServiceConfiguration");
        if (rpc_reply.has_value()) {
            auto const reply = *rpc_reply;
            auto* const ret_ptr = rpc.buffers.return_value;
            EXPECT_CALL(*rpc.method, DoCall(0U))
                .WillOnce([reply, ret_ptr](std::size_t) -> score::Result<void> {
                    if (!reply.has_value()) {
                        return score::MakeUnexpected<void>(reply.error());
                    }
                    *ret_ptr = *reply;
                    return {};
                })
                .RetiresOnSaturation();
        }
        return rpc;
    }

    static void EnsureRealRuntimeInitialized() {
        static std::once_flag init_flag{};
        std::call_once(init_flag, []() {
            auto const path =
                std::filesystem::temp_directory_path() /
                ("gib_ut_mw_com_" + std::to_string(static_cast<long>(::getpid())) + ".json");
            {
                std::ofstream file{path, std::ios::out | std::ios::trunc};
                file << R"({"serviceTypes":[],"serviceInstances":[],"global":{"asil-level":"QM"}})";
            }
            score::mw::com::runtime::RuntimeConfiguration const runtime_cfg{
                score::filesystem::Path{path.string()}};
            score::mw::com::runtime::InitializeRuntime(runtime_cfg);
            std::error_code ec{};
            (void)std::filesystem::remove(path, ec);

            auto* const saved_mock = &score::mw::com::impl::Runtime::getInstance();
            score::mw::com::impl::Runtime::InjectMock(nullptr);
            real_runtime_ = &score::mw::com::impl::Runtime::getInstance();
            if (saved_mock != real_runtime_) {
                score::mw::com::impl::Runtime::InjectMock(saved_mock);
            }
        });
    }

    static score::mw::com::impl::IRuntime& RealRuntime() {
        EnsureRealRuntimeInitialized();
        return *real_runtime_;
    }

    static std::vector<score::mw::com::impl::InstanceIdentifier> ResolveInRealRuntime(
        std::string_view const specifier) {
        auto const parsed =
            score::mw::com::impl::InstanceSpecifier::Create(std::string{specifier}).value();
        return RealRuntime().resolve(parsed);
    }

   private:
    inline static score::mw::com::impl::IRuntime* real_runtime_{nullptr};
};

class ScopedThread final {
   public:
    ScopedThread() noexcept = default;
    explicit ScopedThread(std::thread thread) noexcept : thread_{std::move(thread)} {}
    ~ScopedThread() noexcept { Join(); }

    ScopedThread(ScopedThread const&) = delete;
    ScopedThread& operator=(ScopedThread const&) = delete;

    ScopedThread(ScopedThread&& other) noexcept : thread_{std::move(other.thread_)} {}
    ScopedThread& operator=(ScopedThread&& other) noexcept {
        if (this != &other) {
            Join();
            thread_ = std::move(other.thread_);
        }
        return *this;
    }

    void Join() noexcept {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

   private:
    std::thread thread_{};
};

class PromiseGate final {
   public:
    PromiseGate() : future_{promise_.get_future().share()} {}
    ~PromiseGate() noexcept = default;

    PromiseGate(PromiseGate const&) = delete;
    PromiseGate& operator=(PromiseGate const&) = delete;
    PromiseGate(PromiseGate&&) = delete;
    PromiseGate& operator=(PromiseGate&&) = delete;

    void Signal() noexcept {
        std::call_once(once_, [this]() { promise_.set_value(); });
    }

    [[nodiscard]] std::shared_future<void> Future() const noexcept { return future_; }

    template <typename Rep, typename Period>
    [[nodiscard]] bool WaitFor(std::chrono::duration<Rep, Period> const timeout) const {
        auto future_copy = future_;
        return future_copy.wait_for(timeout) == std::future_status::ready;
    }

   private:
    std::promise<void> promise_{};
    std::shared_future<void> const future_;
    std::once_flag once_{};
};

class ScopedGateSignaler final {
   public:
    explicit ScopedGateSignaler(PromiseGate& gate) noexcept : gate_{gate} {}
    ~ScopedGateSignaler() noexcept { gate_.Signal(); }

    ScopedGateSignaler(ScopedGateSignaler const&) = delete;
    ScopedGateSignaler& operator=(ScopedGateSignaler const&) = delete;
    ScopedGateSignaler(ScopedGateSignaler&&) = delete;
    ScopedGateSignaler& operator=(ScopedGateSignaler&&) = delete;

   private:
    PromiseGate& gate_;
};

template <typename Fn>
class ScopeExit final {
   public:
    explicit ScopeExit(Fn fn) noexcept : fn_{std::move(fn)} {}
    ~ScopeExit() noexcept { fn_(); }

    ScopeExit(ScopeExit const&) = delete;
    ScopeExit& operator=(ScopeExit const&) = delete;
    ScopeExit(ScopeExit&&) = delete;
    ScopeExit& operator=(ScopeExit&&) = delete;

   private:
    Fn fn_;
};

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_TEST_UNIT_IPC_BINDING_TEST_FIXTURE_HPP
