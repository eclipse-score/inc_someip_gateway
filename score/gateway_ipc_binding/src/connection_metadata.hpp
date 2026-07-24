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

#ifndef SRC_GATEWAY_IPC_BINDING_SRC_CONNECTION_METADATA
#define SRC_GATEWAY_IPC_BINDING_SRC_CONNECTION_METADATA

#include <algorithm>
#include <cassert>
#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <unordered_map>

#include "key.hpp"

namespace score::gateway_ipc_binding {

// This mapping represents fully established connections
class Connection_metadata {
   public:
    struct Ids {
        Key_t key;
        Remote_handle local_handle;
        Remote_handle remote_handle;
        Shared_memory_metadata local_metadata;
        Shared_memory_metadata remote_metadata;
    };

    using Client_id_to_metadata = std::unordered_map<Client_id, std::vector<Ids>>;

    template <typename Func>
    void for_each_client(Key_t const& key, Func&& func) const {
        auto key_it = m_key_to_client_indices.find(key);
        if (key_it == m_key_to_client_indices.end()) {
            return;
        }

        for (auto const& [client_id, index] : key_it->second) {
            auto client_it = m_id_to_key.find(client_id);
            if (client_it == m_id_to_key.end() || index >= client_it->second.size()) {
                continue;
            }
            func(client_id, client_it->second[index]);
        }
    }

    void add_mapping(Client_id const client_id, Ids ids) {
        assert(!is_mapping_present(client_id, ids) && "Mapping already exists for given client_id");
        auto& ids_vec = m_id_to_key[client_id];
        ids_vec.push_back(std::move(ids));
        auto const index = ids_vec.size() - 1U;
        m_key_to_client_indices[ids_vec[index].key][client_id] = index;
    }

    std::optional<std::reference_wrapper<Ids const>> get_by_remote_handle(
        Client_id const client_id, Remote_handle const remote_handle) const {
        auto it = m_id_to_key.find(client_id);
        if (it != m_id_to_key.end()) {
            for (const auto& ids : it->second) {
                if (ids.remote_handle == remote_handle) {
                    return std::cref(ids);
                }
            }
        }
        return std::nullopt;
    }

    std::optional<std::reference_wrapper<Ids const>> get_by_local_handle(
        Client_id const client_id, Remote_handle const local_handle) const {
        auto it = m_id_to_key.find(client_id);
        if (it != m_id_to_key.end()) {
            for (const auto& ids : it->second) {
                if (ids.local_handle == local_handle) {
                    return std::cref(ids);
                }
            }
        }
        return std::nullopt;
    }

    // might need to return removed metadata for further cleanup
    void remove_client(Client_id const client_id) {
        m_id_to_key.erase(client_id);
        remove_client_from_key_indices(client_id);
    }

    void remove_mapping_for_client_and_key(Client_id const client_id, Key_t const& key) {
        auto it = m_id_to_key.find(client_id);
        if (it != m_id_to_key.end()) {
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [&key](Ids const& ids) { return ids.key == key; }),
                      vec.end());
            if (vec.empty()) {
                m_id_to_key.erase(it);
                remove_client_from_key_indices(client_id);
            } else {
                rebuild_client_key_indices(client_id);
            }
        }
    }

    // might need to return removed metadata for further cleanup
    void remove_mapping(Client_id const client_id, Remote_handle const remote_handle) {
        auto it = m_id_to_key.find(client_id);
        if (it != m_id_to_key.end()) {
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [remote_handle](Ids const& ids) {
                                         return ids.remote_handle == remote_handle;
                                     }),
                      vec.end());
            if (vec.empty()) {
                m_id_to_key.erase(it);
                remove_client_from_key_indices(client_id);
            } else {
                rebuild_client_key_indices(client_id);
            }
        }
    }

    // might need to return removed metadata for further cleanup
    void remove_service(Key_t const& key) {
        for (auto it = m_id_to_key.begin(); it != m_id_to_key.end();) {
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [&key](Ids const& ids) { return ids.key == key; }),
                      vec.end());
            if (vec.empty()) {
                remove_client_from_key_indices(it->first);
                it = m_id_to_key.erase(it);
            } else {
                rebuild_client_key_indices(it->first);
                ++it;
            }
        }
    }

   private:
    bool is_mapping_present(Client_id const client_id, Ids const& ids) const {
        auto it = m_id_to_key.find(client_id);
        if (it != m_id_to_key.end()) {
            for (const auto& existing_ids : it->second) {
                if (existing_ids.local_handle == ids.local_handle || existing_ids.key == ids.key) {
                    assert(existing_ids.local_handle == ids.local_handle &&
                           existing_ids.key == ids.key &&
                           "Inconsistent mapping found for client_id");
                    return true;
                }
            }
        }
        return false;
    }

    void remove_client_from_key_indices(Client_id const client_id) {
        for (auto it = m_key_to_client_indices.begin(); it != m_key_to_client_indices.end();) {
            it->second.erase(client_id);
            if (it->second.empty()) {
                it = m_key_to_client_indices.erase(it);
            } else {
                ++it;
            }
        }
    }

    void rebuild_client_key_indices(Client_id const client_id) {
        remove_client_from_key_indices(client_id);

        auto const client_it = m_id_to_key.find(client_id);
        if (client_it == m_id_to_key.end()) {
            return;
        }

        for (std::size_t i = 0U; i < client_it->second.size(); ++i) {
            m_key_to_client_indices[client_it->second[i].key][client_id] = i;
        }
    }

    Client_id_to_metadata m_id_to_key;
    std::unordered_map<Key_t, std::unordered_map<Client_id, std::size_t>> m_key_to_client_indices;
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_SRC_CONNECTION_METADATA
