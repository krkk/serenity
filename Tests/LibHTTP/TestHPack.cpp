/*
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/MemoryStream.h>
#include <AK/Tuple.h>
#include <LibHTTP/HPack.h>
#include <LibTest/TestCase.h>
#include <initializer_list>

auto decode_and_compare = [](HTTP::HPack::Decoder& decoder, ReadonlyBytes data, std::initializer_list<Tuple<StringView, StringView>>&& expected_list) -> ErrorOr<void> {
    auto stream = make<FixedMemoryStream>(data);
    auto map = TRY(decoder.decode(move(stream)));
    EXPECT(map.size() == expected_list.size());

    auto decoded_it = map.begin();
    auto const* expected_it = expected_list.begin();
    while (expected_it != expected_list.end()) {
        EXPECT(decoded_it->name == expected_it->get<0>());
        EXPECT(decoded_it->value == expected_it->get<1>());
        ++decoded_it;
        ++expected_it;
    }
    return {};
};

TEST_CASE(test_spec_header_field_representation_examples)
{
    auto decoder = HTTP::HPack::Decoder::create_with_http2_table(1 * KiB);

    // C.2.1. Literal Header Field with Indexing
    TRY_OR_FAIL(decode_and_compare(decoder, "@\ncustom-key\rcustom-header"sv.bytes(),
        { { "custom-key"sv, "custom-header"sv } }));

    // C.2.2. Literal Header Field without Indexing
    TRY_OR_FAIL(decode_and_compare(decoder, "\x04\x0c/sample/path"sv.bytes(),
        { { ":path"sv, "/sample/path"sv } }));

    // C.2.3. Literal Header Field Never Indexed
    TRY_OR_FAIL(decode_and_compare(decoder, "\x10\x08password\x06secret"sv.bytes(),
        { { "password"sv, "secret"sv } }));

    // C.2.4. Indexed Header Field
    TRY_OR_FAIL(decode_and_compare(decoder, "\x82"sv.bytes(), { { ":method"sv, "GET"sv } }));
}

// C.3. Request Examples without Huffman Coding
TEST_CASE(test_spec_request_examples_no_huffman)
{
    auto decoder = HTTP::HPack::Decoder::create_with_http2_table(10 * KiB);

    // C.3.1. First Request
    TRY_OR_FAIL(decode_and_compare(decoder, "\x82\x86\x84\x41\x0fwww.example.com"sv.bytes(),
        {
            { ":method"sv, "GET"sv },
            { ":scheme"sv, "http"sv },
            { ":path"sv, "/"sv },
            { ":authority"sv, "www.example.com"sv },
        }));

    // C.3.2. Second Request
    TRY_OR_FAIL(decode_and_compare(decoder, "\x82\x86\x84\xbe\x58\x08no-cache"sv.bytes(),
        {
            { ":method"sv, "GET"sv },
            { ":scheme"sv, "http"sv },
            { ":path"sv, "/"sv },
            { ":authority"sv, "www.example.com"sv },
            { "cache-control"sv, "no-cache"sv },
        }));

    // C.3.3. Third Request
    TRY_OR_FAIL(decode_and_compare(decoder, "\x82\x87\x85\xbf@\ncustom-key\fcustom-value"sv.bytes(),
        {
            { ":method"sv, "GET"sv },
            { ":scheme"sv, "https"sv },
            { ":path"sv, "/index.html"sv },
            { ":authority"sv, "www.example.com"sv },
            { "custom-key"sv, "custom-value"sv },
        }));
}

// C.4. Request Examples with Huffman Coding
TEST_CASE(test_spec_request_examples_huffman)
{
    auto decoder = HTTP::HPack::Decoder::create_with_http2_table(10 * KiB);

    // C.4.1. First Request
    u8 const first_request[] = { 0x82, 0x86, 0x84, 0x41, 0x8c, 0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff };
    TRY_OR_FAIL(decode_and_compare(decoder, first_request,
        {
            { ":method"sv, "GET"sv },
            { ":scheme"sv, "http"sv },
            { ":path"sv, "/"sv },
            { ":authority"sv, "www.example.com"sv },
        }));

    // C.4.2. Second Request
    u8 const second_request[] = { 0x82, 0x86, 0x84, 0xbe, 0x58, 0x86, 0xa8, 0xeb, 0x10, 0x64, 0x9c, 0xbf };
    TRY_OR_FAIL(decode_and_compare(decoder, second_request,
        {
            { ":method"sv, "GET"sv },
            { ":scheme"sv, "http"sv },
            { ":path"sv, "/"sv },
            { ":authority"sv, "www.example.com"sv },
            { "cache-control"sv, "no-cache"sv },
        }));

    // C.4.3. Third Request
    u8 const third_request[] = {
        0x82, 0x87, 0x85, 0xbf, 0x40, 0x88, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f, 0x89, 0x25,
        0xa8, 0x49, 0xe9, 0x5b, 0xb8, 0xe8, 0xb4, 0xbf
    };
    TRY_OR_FAIL(decode_and_compare(decoder, third_request,
        {
            { ":method"sv, "GET"sv },
            { ":scheme"sv, "https"sv },
            { ":path"sv, "/index.html"sv },
            { ":authority"sv, "www.example.com"sv },
            { "custom-key"sv, "custom-value"sv },
        }));
}
