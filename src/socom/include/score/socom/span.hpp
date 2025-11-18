///
/// \file
/// \copyright Copyright (C) 2015-2020, Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
///

// copy with small modifications from baselibs to be able to compile socom as shared library

#ifndef SCORE_SOCOM_SPAN_HPP
#define SCORE_SOCOM_SPAN_HPP

#include <cstddef>
#include <iterator>
#include <limits>
#include <type_traits>

namespace score::socom {
inline namespace v2 {

/// \brief Provides a 1d-view of a pointer + size. The benefit is that a one-dimensional span does not decay to T*
/// (thus looses the size information).
///
/// If used in combination with a two-dimensional span it represents a row.
///
/// It behaves the same as std::array<T, size_>. Thus you should directly use the container from the
/// C++ standard library. But it is still better than a plain C-Array. If you still want to use it
/// than have a look at the documentation for a two-dimensional span as both similar except the 1d
/// vs 2d view.
///
/// \tparam T Specifies the value type of a single element of a row.
template<typename T>
class span {
    /// @brief Used to obtain the iterator type of the type Range.
    template<typename Range>
    using iterator_t = decltype(std::begin(std::declval<Range&>()));

    template<typename Iterator, typename R = decltype(*std::declval<Iterator&>()), typename = R&>
    using iter_reference_helper = R;

    /// @brief Computes the reference type of Iterator.
    template<typename Iterator>
    using iter_reference_t = iter_reference_helper<Iterator>;

public:
    /// \brief The (cv-qualified) type of the array elements.
    using element_type = T;

    /// \brief The (non cv-qualified) type of the array elements.
    using value_type = std::remove_cv_t<T>;

    /// \brief The type to hold a size information
    using size_type = std::size_t;

    /// @brief Unit of increment between two valid pointers into the span of T
    using difference_type = std::ptrdiff_t;

    /// @brief Type of pointer to element
    using pointer = element_type*;

    /// @brief Type of pointer to const to the element
    using const_pointer = const element_type*;

    /// @brief Type of reference to the element
    using reference = element_type&;

    /// @brief Type of reference to const to the element
    using const_reference = const element_type&;

    /// \brief The type of a non-const iterator
    using iterator = element_type*;

    /// \brief The type of a const iterator
    using const_iterator = element_type const*;

    /// \brief The type of a non-const reverse iterator. This is a requirement of STL compliant types.
    using reverse_iterator = std::reverse_iterator<iterator>;

    /// \brief The type of a const reverse iterator. This is a requirement of STL compliant types.
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    /// \brief Constructs an empty span whose `data() == nullptr` and `size() == 0`.
    span() noexcept : data_{nullptr}, size_{0} {}

    /// \brief Provides a 1d-view of a pointer + size. The benefit is that a one-dimensional span does not decay to
    /// T* (thus looses the size information).
    ///
    /// \param array Pointer to the array
    /// \param size Number of elements in the one-dimensional span
    span(T* const array, const size_type size) : data_{array}, size_{size} {}

    /// \brief Provides a 1d-view of a C-array. The benefit is that a one-dimensional span does not decay to T*
    /// (thus looses the size information).
    ///
    /// \tparam Size Number of elements in the one-dimensional span
    /// \param array Pointer to the array
    // template<std::size_t Size>
    // span(T (&array)[Size]) noexcept : data_{score::cpp::data(array)}, size_{Size}
    // {
    //     static_assert(Size >= 0, "Size must be positive.");
    // }

    /// \brief Constructs a span that is a view over \p range; the resulting span has size() == range.size() and
    /// data() == score::cpp::data(range).
    ///
    /// This overload participates in overload resolution only if Range is iterable, not a span or
    /// an array and elements of the range are convertible to this span's element_type.
    ///
    /// \tparam Range Type of \p range
    /// \param range Range to construct a view for
    template<
        typename Range, typename R = std::decay_t<Range>,
        typename = std::enable_if_t<
            // score::cpp::is_iterable<R>::value &&
            std::is_convertible<std::remove_reference_t<iter_reference_t<iterator_t<Range>>> (*)[],
                                T (*)[]>::value &&
            /* !is_span<R>::value && */ !std::is_array<R>::value>>
    span(Range&& range) : data_{range.data()}, size_{range.size()}
    {
    }

    /// \brief Returns the number elements in the row - when combined with a two-dimensional span the number of
    /// columns.
    ///
    /// \return  Number of elements in the row.
    size_type size() const noexcept { return size_; }

    /// \brief checks if the span is empty
    ///
    /// @return true if span is empty (size==0); false otherwise.
    bool empty() const noexcept { return size() == 0; }

    /// \brief Returns a pointer to the underlying array.
    ///
    /// \return Pointer to the first element of the row.
    pointer data() const noexcept { return data_; }

    ///\{
    /// \brief Returns an iterator to the beginning
    ///
    /// \return Iterator to the first element in the row.
    iterator begin() const noexcept { return iterator{this->data_}; }
    const_iterator cbegin() const noexcept { return const_iterator{this->data_}; }
    ///\}

    ///\{
    /// \brief Returns an iterator one past the end
    ///
    /// Attempting to access it results in undefined behavior.
    ///
    /// \return Iterator one past the last element of the row.
    iterator end() const noexcept { return iterator{this->data_ + this->size_}; }
    const_iterator cend() const noexcept { return const_iterator{this->data_ + this->size_}; }
    ///\}

    /// \{
    /// \brief Returns an iterator to the reverse-beginning of the container
    ///
    /// \return Iterator to the last element of the span.
    reverse_iterator rbegin() const noexcept { return reverse_iterator{end()}; }
    const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator{cend()}; }
    /// \}

    /// \{
    /// \brief Returns an iterator to the reverse-end of the container
    ///
    /// Attempting to access it results in undefined behavior.
    ///
    /// \return Iterator one before the first element of the container.
    reverse_iterator rend() const noexcept { return reverse_iterator{begin()}; }
    const_reverse_iterator crend() const noexcept { return const_reverse_iterator{cbegin()}; }
    /// \}

    /// \brief Returns a reference to the first element in the span
    ///
    /// \pre !empty()
    ///
    /// \return A reference to the first element.
    reference front() const { return *begin(); }

    /// \brief Returns a reference to the last element in the span
    ///
    /// \pre !empty()
    ///
    /// \return A reference to the last element.
    reference back() const { return *(end() - 1); }

    /// \brief Obtains a span that is a view over the `count` elements of this span starting at offset `offset`. If
    /// `count` is dynamic_extent, the subspan contains `size() - offset` elements (i.e., all remaining elements).
    ///
    /// \param offset skipped elements at the front
    /// \param count number of elements in the resulting subspan, if `count` is  dynamic_extent, the resulting subspan
    /// contains all remaining elements.
    /// \pre `0 <= offset <= size()` \pre `count <= size() - offset`
    /// \return The requested subspan `r`, such that `r.data() == this->data() + offset`. If `count` is dynamic_extent,
    /// `r.size() == this->size() - offset`; otherwise `r.size() == count`.
    span subspan(const size_type offset, const size_type count = dynamic_extent) const
    {
        return span{data() + offset, (count != dynamic_extent) ? count : (size() - offset)};
    }

    /// \brief Obtains a span that is a view over the first `count` elements of this span.
    ///
    /// \param count number of elements in the resulting subspan
    /// \pre `0 <= count <= size()`
    /// \return A span `r` that is a view over the first `count` elements of `*this`, such that
    /// `r.data() == this->data() && r.size() == count`.
    span first(const size_type count) const { return span{data(), count}; }

    /// \brief Obtains a span that is a view over the last `count` elements of this span.
    ///
    /// \param count number of elements in the resulting subspan
    /// \pre `0 <= count <= size()`
    /// \return A span `r` that is a view over the last `count` elements of `*this`, such that
    /// `r.data() == this->data() + (this->size() - count) && r.size() == count`.
    span last(const size_type count) const { return span{data() + (size() - count), count}; }

private:
    /// \brief The max value of type to hold a size information
    static constexpr size_type dynamic_extent = std::numeric_limits<size_type>::max();

    /// \brief Pointer to the underlying array. Points to the first element of the row.
    pointer data_;

    /// \brief Size of the least significant dimension of the array -  when combined with a two-dimensional span
    /// the number of columns.
    size_type size_;
};

} // namespace v2
} // namespace score::socom

#endif // SCORE_LANGUAGE_FUTURECPP_SPAN_HPP
