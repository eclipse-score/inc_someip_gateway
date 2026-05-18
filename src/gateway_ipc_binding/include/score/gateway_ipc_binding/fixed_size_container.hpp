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

#ifndef SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_FIXED_SIZE_CONTAINER
#define SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_FIXED_SIZE_CONTAINER

#include <score/result/result.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <score/gateway_ipc_binding/error.hpp>
#include <string>
#include <string_view>

namespace score::gateway_ipc_binding {

/// \brief Fixed-size container with explicit length for POD wire format
/// \tparam T Type of elements in the container
/// \tparam Max_size Maximum number of bytes available in data
///
/// \note A similar data structure is score::cpp::inplace_vector, but that fails to be trivially
/// copyable due to its non-trivial destructor, copy and move operations, which makes it unsuitable
/// for IPC payloads. See https://github.com/eclipse-score/baselibs/issues/168 for details.
template <typename T, std::size_t Max_size>
struct Fixed_size_container {
    std::size_t size;
    std::array<T, Max_size> data;
    static constexpr std::size_t max_size = Max_size;

    Fixed_size_container() = default;

    template <std::size_t N, typename = std::enable_if_t<N <= Max_size>>
    Fixed_size_container(T const (&arr)[N]) : size{N} {
        std::copy_n(arr, N, data.begin());
    }

    Fixed_size_container(std::initializer_list<T> initializer_list)
        : size(initializer_list.size()) {
        assert(initializer_list.size() <= Max_size);
        std::copy_n(initializer_list.begin(), std::min(initializer_list.size(), Max_size),
                    data.begin());
    }

    bool empty() const noexcept { return size == 0; }
};

template <typename T, std::size_t Max_size>
bool operator==(Fixed_size_container<T, Max_size> const& lhs,
                Fixed_size_container<T, Max_size> const& rhs) {
    return lhs.size == rhs.size &&
           std::equal(std::begin(lhs.data), std::next(std::begin(lhs.data), lhs.size),
                      std::begin(rhs.data), std::next(std::begin(rhs.data), rhs.size));
}

struct Fixed_size_container_hash {
    template <typename T, std::size_t Max_size>
    std::size_t operator()(Fixed_size_container<T, Max_size> const& s) const noexcept {
        std::size_t h1 = std::hash<std::uint16_t>{}(s.size);
        std::size_t h2 = std::hash<std::string_view>{}(std::string_view{s.data.data(), s.size});
        return h1 ^ (h2 << 1);
    }
};

/// \brief Fixed-size string with explicit length for POD wire format
/// \tparam Max_size Maximum number of bytes available in data
template <std::size_t Max_size>
using Fixed_string = Fixed_size_container<char, Max_size>;

template <typename Target_type>
Result<Fixed_string<Target_type::max_size>> fixed_string_from_string(
    std::string_view value) noexcept {
    if (value.size() > Target_type::max_size) {
        return MakeUnexpected(Gateway_ipc_binding_error::fixed_size_container_too_small);
    }

    Fixed_string<Target_type::max_size> result{};
    result.size = value.size();
    std::memcpy(result.data.data(), value.data(), value.size());
    return result;
}

template <std::size_t Max_size>
std::string fixed_string_to_string(Fixed_string<Max_size> const& value) {
    return std::string(value.data.data(), value.size);
}

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_FIXED_SIZE_CONTAINER
