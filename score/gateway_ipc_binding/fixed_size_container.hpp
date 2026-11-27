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

#ifndef SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_FIXED_SIZE_CONTAINER
#define SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_FIXED_SIZE_CONTAINER

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <score/assert.hpp>
#include <type_traits>

namespace score::gateway_ipc_binding {

/// \brief Trivially copyable fixed-capacity container with explicit element count for IPC wire
/// data.
/// \tparam T Element type stored in data (must be trivially copyable for shared-memory transport).
/// \tparam Max_size Maximum number of elements of type T that data can hold.
///
/// \details Invariants:
///          - Both default initialization and value initialization set `size` to `0` and
///            value-initialize all `Max_size` elements of `data`.
///          - Array and `std::initializer_list` (`initializer_list.size() <= Max_size`)
///            constructors copy the input elements into `data[0..size-1]` and value-initialize
///            trailing elements `data[size..Max_size-1]`.
///          - `operator==` and `operator!=` guard against corrupted shared-memory headers: if
///            `lhs.size > Max_size || rhs.size > Max_size`, `operator==` returns `false` (and
///            `operator!=` returns `true`) without reading past `data[Max_size - 1]`; otherwise
///            elements `[0..size-1]` are compared.
/// \note A similar data structure is score::cpp::inplace_vector, but that fails to be trivially
/// copyable due to its non-trivial destructor, copy and move operations, which makes it unsuitable
/// for IPC payloads. See https://github.com/eclipse-score/baselibs/issues/168 for details.
template <typename T, std::size_t Max_size>
struct Fixed_size_container {
    std::size_t size{0U};
    std::array<T, Max_size> data{};
    static constexpr std::size_t max_size = Max_size;

    constexpr Fixed_size_container() noexcept = default;

    template <std::size_t N, typename = std::enable_if_t<N <= Max_size>>
    Fixed_size_container(T const (&arr)[N]) noexcept(std::is_nothrow_copy_assignable_v<T>)
        : size{N}, data{} {
        std::copy_n(arr, N, data.begin());
    }

    Fixed_size_container(std::initializer_list<T> initializer_list)
        : size{std::min(initializer_list.size(), Max_size)}, data{} {
        SCORE_LANGUAGE_FUTURECPP_ASSERT(initializer_list.size() <= Max_size);
        std::copy_n(initializer_list.begin(), size, data.begin());
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return size == 0U; }
};

template <typename T, std::size_t Max_size>
bool operator==(Fixed_size_container<T, Max_size> const& lhs,
                Fixed_size_container<T, Max_size> const& rhs) noexcept {
    if ((lhs.size > Max_size) || (rhs.size > Max_size) || (lhs.size != rhs.size)) {
        return false;
    }
    return std::equal(lhs.data.cbegin(),
                      std::next(lhs.data.cbegin(), static_cast<std::ptrdiff_t>(lhs.size)),
                      rhs.data.cbegin());
}

template <typename T, std::size_t Max_size>
bool operator!=(Fixed_size_container<T, Max_size> const& lhs,
                Fixed_size_container<T, Max_size> const& rhs) noexcept {
    return !(lhs == rhs);
}

}  // namespace score::gateway_ipc_binding

#endif  // SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_FIXED_SIZE_CONTAINER
