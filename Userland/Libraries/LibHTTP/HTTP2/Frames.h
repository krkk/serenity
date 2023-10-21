/*
 * Copyright (c) 2023, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Endian.h>
#include <AK/Stream.h>
#include <AK/Types.h>

namespace HTTP::HTTP2 {

// https://httpwg.org/specs/rfc9113.html#FrameTypes
enum class FrameType : u8 {
    DATA,
    HEADERS,
    PRIORITY,
    RST_STREAM,
    SETTINGS,
    PUSH_PROMISE,
    PING,
    GOAWAY,
    WINDOW_UPDATE,
    CONTINUATION
};

// https://httpwg.org/specs/rfc9113.html#FrameHeader
struct Frame {
    NetworkOrdered<u32> length;
    FrameType type;
    u8 flags;

    // The highest bit is reserved and must be left unset.
    u32 stream_identifier;

    ByteBuffer payload;

    static ErrorOr<Frame> read_from_stream(Stream& stream)
    {
        Frame frame;

        auto length_and_type = TRY(stream.read_value<NetworkOrdered<u32>>());
        frame.length = length_and_type >> 8;
        frame.type = static_cast<FrameType>(length_and_type & 0xff);

        frame.stream_identifier = TRY(stream.read_value<NetworkOrdered<u32>>());
        if ((frame.stream_identifier & 0x80000000) != 0)
            return Error::from_string_literal("Highest bit in stream identifier was not left unset");

        auto buffer = TRY(ByteBuffer::create_uninitialized(frame.length));
        TRY(stream.read_until_filled(buffer));
        return frame;
    }

    ErrorOr<void> write_to_stream(Stream& stream) const
    {
        VERIFY(length <= 0xff'ffff);
        VERIFY(length == payload.size());
        VERIFY((stream_identifier & 0x80000000) == 0);

        // length is actually a u24.
        TRY(stream.write_until_depleted({ &length + 1, 3 }));

        TRY(stream.write_value(type));
        TRY(stream.write_value(flags));
        TRY(stream.write_value<NetworkOrdered<decltype(stream_identifier)>>(stream_identifier));
        TRY(stream.write_until_depleted(payload));
        return {};
    }
};

namespace Headers {

// https://httpwg.org/specs/rfc9113.html#rfc.section.6.2.p.4
enum class Flags : u8 {
    END_STREAM = 0x01,
    END_HEADERS = 0x04,
    PADDED = 0x08,
    PRIORITY = 0x20,
};

}
namespace Settings {

enum class Flags : u8 {
    ACK = 1
};

// 6.5.2. Defined Settings, https://httpwg.org/specs/rfc9113.html#SettingValues
enum class SettingIdentifier : u16 {
    SETTINGS_HEADER_TABLE_SIZE = 1,
    SETTINGS_ENABLE_PUSH,
    SETTINGS_MAX_CONCURRENT_STREAMS,
    SETTINGS_INITIAL_WINDOW_SIZE,
    SETTINGS_MAX_FRAME_SIZE,
    SETTINGS_MAX_HEADER_LIST_SIZE,
};

struct Setting {
    NetworkOrdered<SettingIdentifier> identifier;
    NetworkOrdered<u32> value;
};

}

};
