/********************************************************************************
 * Copyright (c) 2025 Elektrobit Automotive GmbH
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

#include "make_vector_buffer.hpp"

#include <score/socom/payload.hpp>
#include <score/socom/vector_payload.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <iterator>
#include <memory>
#include <random>
#include <type_traits>
#include <vector>

namespace socom = ::score::socom;

using ::score::socom::test::make_vector_buffer;
using socom::empty_payload;
using socom::make_vector_payload;
using socom::Payload;
using socom::Vector_buffer;
using ::testing::Combine;
using ::testing::TestWithParam;
using ::testing::Values;

namespace {

static_assert(!std::is_move_assignable<Payload>::value, "");
static_assert(!std::is_move_constructible<Payload>::value, "");
static_assert(!std::is_copy_assignable<Payload>::value, "");
static_assert(!std::is_copy_constructible<Payload>::value, "");

void increase_and_fill(::score::socom::Vector_buffer& data, std::size_t const new_size)
{
    auto const old_size = data.size();
    if (new_size <= old_size) {
        return;
    }
    data.resize(new_size);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(
        std::numeric_limits<std::underlying_type_t<Payload::Byte>>::min(),
        std::numeric_limits<std::underlying_type_t<Payload::Byte>>::max());
    auto start = std::begin(data);
    std::advance(start, old_size);
    std::generate(start, std::end(data), [&gen, &distrib]() { return std::byte(distrib(gen)); });
}


Vector_buffer create_vector_payload_with_random_data(std::size_t const size)
{
    Vector_buffer data;
    increase_and_fill(data, size);
    return data;
}

Payload::Span create_span_with_offsets(Vector_buffer const& cont, std::size_t const start_offset,
                                       std::size_t const end_offset)
{
    auto begin = std::begin(cont);
    std::advance(begin, start_offset);
    return Payload::Span{&*begin, end_offset - start_offset};
}

template<typename SPAN, typename CONT>
void check_span(SPAN const& span, CONT const& cont)
{
    ASSERT_EQ(cont.size(), span.size());
    EXPECT_TRUE(std::equal(std::begin(cont), std::end(cont), std::begin(span)));
}

void check_payload(Payload const& payload, Vector_buffer const& cont)
{
    check_span(payload.data(), cont);
}

Vector_buffer add_buffers(Vector_buffer header, Vector_buffer const& payload)
{
    header.insert(std::end(header), std::begin(payload), std::end(payload));
    return header;
}

TEST(Payload, NoPayloadConstruct)
{
    EXPECT_EQ(0, empty_payload()->data().size());
}

TEST(Payload, NoPayloadCopyConstruct)
{
    Payload::Sptr const copy{empty_payload()};

    EXPECT_EQ(empty_payload(), copy);
    EXPECT_EQ(0, copy->data().size());
}

TEST(Payload, VectorPayloadConstructInitializerList)
{
    auto const payload =
        make_vector_payload({std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}});
    check_payload(*payload, make_vector_buffer(1U, 2U, 3U, 4U, 5U));
}

TEST(Payload, VectorPayloadConstructVectorLVR)
{
    auto const temp_buffer = make_vector_buffer(1U, 2U, 3U, 4U, 5U);
    auto const payload = make_vector_payload(temp_buffer);
    check_payload(*payload, temp_buffer);
}

class PayloadOperatorEqualTest
    : public TestWithParam<std::tuple<Vector_buffer, Vector_buffer, Vector_buffer, Vector_buffer>> {
protected:
    Vector_buffer const& m_lhs_header{std::get<0>(GetParam())};
    Vector_buffer const& m_lhs_payload{std::get<1>(GetParam())};
    Vector_buffer const& m_rhs_header{std::get<2>(GetParam())};
    Vector_buffer const& m_rhs_payload{std::get<3>(GetParam())};

    Payload::Sptr m_lhs{
        make_vector_payload(m_lhs_header.size(), add_buffers(m_lhs_header, m_lhs_payload))};
    Payload::Sptr m_rhs{
        make_vector_payload(m_rhs_header.size(), add_buffers(m_rhs_header, m_rhs_payload))};
};

Vector_buffer const header0{make_vector_buffer(0U, 1U, 2U, 3U)};
Vector_buffer const header1{make_vector_buffer(4U, 5U, 1U, 3U, 3U, 32U, 3U)};
Vector_buffer const header2{};
Vector_buffer const payload0{make_vector_buffer(0U, 1U, 3U, 40U, 31U, 3U)};
Vector_buffer const payload1{make_vector_buffer(7U, 6U, 4U, 7U, 9U)};
Vector_buffer const payload2{};

auto const headers = Values(header0, header1, header2);
auto const payloads = Values(payload0, payload1, payload2);

INSTANTIATE_TEST_SUITE_P(OperatorEqual, PayloadOperatorEqualTest,
                         Combine(headers, payloads, headers, payloads));

TEST_P(PayloadOperatorEqualTest, OperatorEqualComparesPayloads)
{
    if ((m_lhs_header == m_rhs_header) && (m_lhs_payload == m_rhs_payload)) {
        EXPECT_EQ(*m_lhs, *m_rhs);
    }
    else {
        EXPECT_NE(*m_lhs, *m_rhs);
    }
}

class VectorPayloadtest : public TestWithParam<std::tuple<std::size_t, std::size_t>> {
protected:
    std::size_t const m_size{std::get<0>(GetParam())};
    std::size_t const m_start_offset{std::get<1>(GetParam())};

    Vector_buffer const m_data{create_vector_payload_with_random_data(m_size)};

    Payload::Sptr m_payload{make_vector_payload(m_start_offset, Vector_buffer{m_data})};
};

INSTANTIATE_TEST_SUITE_P(Offsets, VectorPayloadtest,
                         Combine(Values(100), Values(0, 1, 10, 50, 90, 99, 100)));

INSTANTIATE_TEST_SUITE_P(Size, VectorPayloadtest, Combine(Values(100, 2134, 1000), Values(10)));

TEST_P(VectorPayloadtest, SpanReturnsDataAfterTheHeader)
{
    EXPECT_EQ(m_size - m_start_offset, m_payload->data().size());
    check_span(m_payload->data(), create_span_with_offsets(m_data, m_start_offset, m_size));
}

TEST_P(VectorPayloadtest, HeaderReturnsDataBeforeSpan)
{
    EXPECT_EQ(m_start_offset, m_payload->header().size());
    check_span(m_payload->header(), create_span_with_offsets(m_data, 0, m_start_offset));
}

TEST_P(VectorPayloadtest, HeaderAndSpanAreNextToEachOther)
{
    auto const& const_payload = *m_payload;
    EXPECT_EQ(const_payload.header().end(), m_payload->data().begin());
}

TEST_P(VectorPayloadtest, HeaderSizeEqualsStartOffset)
{
    EXPECT_EQ(m_start_offset, m_payload->header().size());
}

class VectorPayloadLeadOffsetTest
    : public TestWithParam<std::tuple<std::size_t, std::size_t, std::size_t>> {
protected:
    std::size_t const m_size{std::get<0>(GetParam())};
    std::size_t const m_lead_offset{std::get<1>(GetParam())};
    std::size_t const m_header_size{std::get<2>(GetParam())};

    Vector_buffer const m_data{create_vector_payload_with_random_data(m_size)};

    Payload::Sptr m_payload{
        make_vector_payload(m_lead_offset, m_header_size, Vector_buffer{m_data})};
};

INSTANTIATE_TEST_SUITE_P(Offsets, VectorPayloadLeadOffsetTest,
                         Combine(Values(100), Values(0, 1, 5, 10, 20), Values(0, 1, 20, 50, 80)));

TEST_P(VectorPayloadLeadOffsetTest, SpanReturnsDataAfterTheHeader)
{
    EXPECT_EQ(m_size - m_header_size - m_lead_offset, m_payload->data().size());
    check_span(m_payload->data(),
               create_span_with_offsets(m_data, m_lead_offset + m_header_size, m_size));
}

TEST_P(VectorPayloadLeadOffsetTest, HeaderReturnsDataBeforeSpan)
{
    EXPECT_EQ(m_header_size, m_payload->header().size());
    check_span(m_payload->header(),
               create_span_with_offsets(m_data, m_lead_offset, m_lead_offset + m_header_size));
}

TEST_P(VectorPayloadLeadOffsetTest, HeaderAndSpanAreNextToEachOther)
{
    auto const& const_payload = *m_payload;
    EXPECT_EQ(const_payload.header().end(), m_payload->data().begin());
}

class VectorPayloadDeathTest : public TestWithParam<std::size_t> {
protected:
    std::size_t const m_size{GetParam()};
    Vector_buffer const m_data{create_vector_payload_with_random_data(m_size)};

    Payload::Sptr m_payload{make_vector_payload(m_data)};
};

INSTANTIATE_TEST_SUITE_P(Offset, VectorPayloadDeathTest, Values(10, 100, 1000));

TEST_P(VectorPayloadDeathTest, MakeVectorPayloadWithHeaderSizeBiggerThanBufferAsserts)
{
    EXPECT_NO_FATAL_FAILURE(make_vector_payload(m_size, Vector_buffer{m_data}));

    EXPECT_DEATH(make_vector_payload(m_size + 1, Vector_buffer{m_data}), "[Aa]ssertion .*failed");
}

TEST_P(VectorPayloadDeathTest, MakeVectorPayloadWithLeadOffsetBiggerThanBufferAsserts)
{
    EXPECT_NO_FATAL_FAILURE(make_vector_payload(0UL, m_size, Vector_buffer{m_data}));

    EXPECT_DEATH(make_vector_payload(1UL, m_size, Vector_buffer{m_data}), "[Aa]ssertion .*failed");
}

TEST(Payload, OperatorEqual)
{
    auto const payload0 = make_vector_payload(make_vector_buffer(1U, 2U, 3U, 4U, 5U));
    auto const payload0_same = make_vector_payload(make_vector_buffer(1U, 2U, 3U, 4U, 5U));
    auto const payload1 = make_vector_payload(make_vector_buffer(1U, 2U, 3U, 32U, 43U, 43U));
    auto const payload2 = make_vector_payload(make_vector_buffer(1U, 2U, 2U, 32U, 43U, 43U));
    auto const payload3 = make_vector_payload({});

    EXPECT_EQ(*payload0, *payload0);
    EXPECT_EQ(*payload0, *payload0_same);
    EXPECT_EQ(*payload1, *payload1);
    EXPECT_EQ(*payload2, *payload2);
    EXPECT_EQ(*payload3, *payload3);
    EXPECT_NE(*payload0, *payload1);
    EXPECT_NE(*payload0, *payload2);
    EXPECT_NE(*payload0, *payload3);
    EXPECT_NE(*payload1, *payload2);
    EXPECT_NE(*payload1, *payload3);
    EXPECT_NE(*payload2, *payload3);
}

} // namespace
