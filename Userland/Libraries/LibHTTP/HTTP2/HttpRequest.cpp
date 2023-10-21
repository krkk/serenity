/*
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "AK/MemoryStream.h"
#include <AK/Base64.h>
#include <AK/StringBuilder.h>
#include <AK/URLParser.h>
#include <LibHTTP/HPack.h>
#include <LibHTTP/HTTP2/Frames.h>
#include <LibHTTP/HttpRequest.h>
#include <LibHTTP/Job.h>

namespace HTTP {

static ErrorOr<void> start_http2_connection(Stream& stream)
{
    // 3.4. HTTP/2 Connection Preface, https://httpwg.org/specs/rfc9113.html#preface
    TRY(stream.write_until_depleted("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"sv.bytes()));

    HTTP2::Frame settings { .type = HTTP2::FrameType::SETTINGS };
    TRY(stream.write_value(settings));

    return {};
}

ErrorOr<ByteBuffer> write_raw_http2_request(Stream& stream, HttpRequest const& request)
{
    TRY(start_http2_connection(stream));

    // 8.1. HTTP Message Framing
    // Streams initiated by a client MUST use odd-numbered stream identifiers
    HTTP2::Frame headers { .type = HTTP2::FrameType::HEADERS, .stream_identifier = 1 };
    auto hpack = HPack::create_with_http2_table(1 * KiB);

    auto path = TRY([request]() -> ErrorOr<String> {
        StringBuilder path;
        // NOTE: The percent_encode is so that e.g. spaces are properly encoded.
        path.append(request.url().serialize_path());
        VERIFY(!path.is_empty());
        TRY(path.try_append(URL::percent_encode(path.to_deprecated_string(), URL::PercentEncodeSet::EncodeURI)));
        if (request.url().query().has_value()) {
            TRY(path.try_append('?'));
            TRY(path.try_append(*request.url().query()));
        }
        return path.to_string();
    }());

    Vector<HPackHeader> hpack_headers {
        { ":method"_string, TRY(String::from_utf8(request.method_name())) },
        { ":scheme"_string, request.url().scheme() },
        { ":authority"_string, TRY(request.url().serialized_host()) },
        { ":path"_string, path },
    };

    for (auto const& header : request.headers())
        hpack_headers.append({ TRY(String::from_deprecated_string(header.name)), TRY(String::from_deprecated_string(header.value)) });
    AllocatingMemoryStream hpack_stream;
    TRY(hpack.encode(hpack_stream, hpack_headers));
    headers.payload.append(TRY(hpack_stream.read_until_eof()));
    headers.length = headers.payload.size();
    TRY(stream.write_value(headers));

    return stream.read_until_eof();
}

}
