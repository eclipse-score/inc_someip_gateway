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

#ifndef SRC_GATEWAY_IPC_BINDING_TEST_MOCKS
#define SRC_GATEWAY_IPC_BINDING_TEST_MOCKS

#include <gmock/gmock.h>

#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <score/gateway_ipc_binding/shared_memory_slot_manager.hpp>
#include <score/socom/move_only_function_mock.hpp>

namespace score::gateway_ipc_binding {

class Shared_memory_manager_factory_mock : public Shared_memory_manager_factory {
   public:
    MOCK_METHOD(Result<Shared_memory_slot_manager::Uptr>, create,
                (score::socom::Service_interface_identifier const& interface,
                 score::socom::Service_instance const& instance),
                (noexcept, override));

    MOCK_METHOD(Result<Read_only_shared_memory_slot_manager::Uptr>, open,
                (Shared_memory_metadata const& metadata), (noexcept, override));
};

class Shared_memory_slot_manager_mock : public Shared_memory_slot_manager {
   public:
    MOCK_METHOD(Result<Shared_memory_slot_guard>, allocate_slot, (), (noexcept, override));

    MOCK_METHOD(Result<void>, add_consumer, (Slot_handle handle), (noexcept, override));

    MOCK_METHOD(Result<void>, release_slot, (Slot_handle handle), (noexcept, override));

    MOCK_METHOD(std::size_t, get_slot_count, (), (const, noexcept, override));

    MOCK_METHOD(std::size_t, get_slot_size, (), (const, noexcept, override));

    MOCK_METHOD(std::size_t, get_allocated_slot_count, (), (const, noexcept, override));

    MOCK_METHOD(std::size_t, get_reference_count, (Slot_handle handle),
                (const, noexcept, override));

    MOCK_METHOD(Result<score::cpp::span<Byte>>, get_memory, (Slot_handle handle),
                (const, noexcept, override));

    MOCK_METHOD(std::string, get_path, (), (const, noexcept, override));

    Shared_memory_slot_guard create_slot_guard(Slot_handle handle, score::cpp::span<Byte> memory) {
        EXPECT_CALL(*this, get_memory(handle))
            .WillRepeatedly(testing::Return(Result<score::cpp::span<Byte>>(memory)));

        return Shared_memory_slot_manager::create_slot_guard(*this, handle);
    }
};

class Read_only_shared_memory_slot_manager_mock : public Read_only_shared_memory_slot_manager {
   public:
    MOCK_METHOD(socom::Payload::Uptr, get_payload,
                (Shared_memory_handle handle, On_payload_destruction_callback callback),
                (const, noexcept, override));
};

using On_find_service_change_mock =
    socom::Move_only_function_mock<Gateway_ipc_binding_server::On_find_service_change>;

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_TEST_MOCKS
