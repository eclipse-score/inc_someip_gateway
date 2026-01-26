#pragma once

#include <score/socom/event.hpp>
#include <vector>

namespace score::socom {
template <typename T, typename Impl>
auto create_impls(Impl& connector, std::size_t const& num_elements) {
    std::vector<T> events;
    events.reserve(num_elements);
    for (Event_id id = 0; id < num_elements; ++id) {
        events.emplace_back(connector, id);
    }
    return events;
}

template <typename T, typename U>
auto create_wrapper_vector(U&& vec) {
    std::vector<std::reference_wrapper<T>> result;
    result.reserve(vec.size());
    for (auto& item : vec) {
        result.emplace_back(item);
    }
    return result;
}
}  // namespace score::socom
