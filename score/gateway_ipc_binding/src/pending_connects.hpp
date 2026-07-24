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

#ifndef SRC_GATEWAY_IPC_BINDING_SRC_PENDING_CONNECTS
#define SRC_GATEWAY_IPC_BINDING_SRC_PENDING_CONNECTS

#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <unordered_map>

#include "key.hpp"

namespace score::gateway_ipc_binding {

class Pending_connects {
   public:
    struct Pending_connect {
        Key_t key;
        Client_id client_id{0};
    };

    using Pending_connects_map = std::unordered_map<Remote_handle, Pending_connect>;

    template <typename Value_checker>
    void clear_pending_connects(Value_checker checker) noexcept {
        for (auto it = m_pending_connects.begin(); it != m_pending_connects.end();) {
            if (checker(it->second)) {
                it = m_pending_connects.erase(it);
            } else {
                ++it;
            }
        }
    }

    void clear_pending_connects_for_key(Key_t const& key) noexcept {
        return clear_pending_connects([&key](auto const& val) { return val.key == key; });
    }

    typename Pending_connects_map::const_iterator find(Remote_handle const remote_handle) const {
        return m_pending_connects.find(remote_handle);
    }

    typename Pending_connects_map::const_iterator end() const { return m_pending_connects.end(); }

    void erase(typename Pending_connects_map::const_iterator it) { m_pending_connects.erase(it); }

    void emplace(Remote_handle const remote_handle, Pending_connect const& t) {
        m_pending_connects.emplace(remote_handle, t);
    }

   private:
    Pending_connects_map m_pending_connects;
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_SRC_PENDING_CONNECTS
