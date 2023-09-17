/*
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ID3.h"
#include <AK/Debug.h>
#include <AK/Endian.h>
#include <AK/MemoryStream.h>
#include <LibCompress/Zlib.h>
#include <LibTextCodec/Decoder.h>

namespace Audio {

// clang-format off
// 3.1. ID3v2 header, ID3 tag version 2.4.0 - Main Structure
enum class ID3v2Flags : u8 {
    UsesUnsynchronization = 0b1000'0000,
    ExtendedHeader        = 0b0100'0000,
    ExperimentalIndicator = 0b0010'0000,
    FooterPresent         = 0b0001'0000,
};
AK_ENUM_BITWISE_OPERATORS(ID3v2Flags)

// 4.1. Frame header flags, ID3 tag version 2.4.0 - Main Structure
enum class FormatFlags : u8 {
    GroupedIdentity  = 0b0100'0000,
    Compressed       = 0b0000'1000,
    Encrypted        = 0b0000'0100,
    Unsynchronizated = 0b0000'0010,
};
AK_ENUM_BITWISE_OPERATORS(FormatFlags)
// clang-format on

struct ID3Header {
    u8 version;
    u8 revision;
    ID3v2Flags flags;
    u32 size;
};

template<bool IsLegacyID3v2_0>
static ErrorOr<void> read_frame(Metadata& metadata_to_write_into, Stream&, bool uses_unsynchronization);

static ErrorOr<u32> read_u24(Stream& stream)
{
    u32 number = 0;
    for (size_t i = 0; i < 3; i++) {
        auto byte = TRY(stream.read_value<u8>());
        number <<= 8;
        number |= byte & 0xFF;
    }
    return number;
}

static ErrorOr<u32> read_synchsafe_u32(Stream& stream)
{
    u32 number = 0;
    for (size_t i = 0; i < 4; i++) {
        // Each byte has a zeroed most significant bit to prevent it from looking like a sync code.
        auto byte = TRY(stream.read_value<u8>());
        number <<= 7;
        number |= byte & 0x7F;
    }
    return number;
}

// Seeks back if an ID3 header couldn't be found.
static ErrorOr<Optional<ID3Header>> read_header(SeekableStream& stream)
{
    // https://web.archive.org/web/20220729070810/https://id3.org/id3v2.4.0-structure?action=raw
    auto identifier = TRY((stream.read_value<Array<u8, 3>>()));
    auto read_identifier = StringView(identifier);
    if (read_identifier == "ID3"sv) {
        auto version = TRY(stream.read_value<u8>());
        auto revision = TRY(stream.read_value<u8>());
        auto flags = TRY(stream.read_value<ID3v2Flags>());
        auto size = TRY(read_synchsafe_u32(stream));

        return ID3Header { version, revision, flags, size };
    }
    if (read_identifier != "TAG"sv)
        MUST(stream.seek(-static_cast<int>(read_identifier.length()), SeekMode::FromCurrentPosition));
    return OptionalNone {};
}

ErrorOr<void> skip_id3(SeekableStream& stream)
{
    auto maybe_header = TRY(read_header(stream));
    if (maybe_header.has_value())
        TRY(stream.discard(maybe_header->size));
    return {};
}

[[maybe_unused]] static StringView id3_version_to_string(u8 version, u8 revision)
{
    if (version == 0x02 && revision == 0x00)
        return "2"sv;
    if (version == 0x03 && revision == 0x00)
        return "2.3"sv;
    if (version == 0x04 && revision == 0x00)
        return "2.4"sv;
    return "(unknown)"sv;
}

ErrorOr<Optional<Metadata>> read_id3_metadata(SeekableStream& stream)
{
    auto maybe_header = TRY(read_header(stream));
    if (!maybe_header.has_value())
        return OptionalNone {};
    auto header = maybe_header.release_value();

    if constexpr (AID3_DEBUG) {
        dbgln("Found ID3v{} header:", id3_version_to_string(header.version, header.revision));
        dbgln("  flags:");
        dbgln("    UsesUnsynchronization: {} ", has_flag(header.flags, ID3v2Flags::UsesUnsynchronization));
        dbgln("    ExtendedHeader: {} ", has_flag(header.flags, ID3v2Flags::ExtendedHeader));
        dbgln("    ExperimentalIndicator: {} ", has_flag(header.flags, ID3v2Flags::ExperimentalIndicator));
        dbgln("    FooterPresent: {} ", has_flag(header.flags, ID3v2Flags::FooterPresent));
        dbgln("  size: {} ", header.size);
    }

    auto data = TRY(ByteBuffer::create_uninitialized(header.size));
    TRY(stream.read_until_filled(data));

    FixedMemoryStream frame_stream(data.bytes());
    if (has_flag(header.flags, ID3v2Flags::ExtendedHeader)) {
        auto size = TRY(read_synchsafe_u32(frame_stream));
        // The size includes the header itself.
        TRY(frame_stream.discard(size - sizeof(u32)));
    }

    bool uses_unsynchronization = has_flag(header.flags, ID3v2Flags::UsesUnsynchronization);
    Metadata metadata;
    while (true) {
        auto result = [&]() -> ErrorOr<void> {
            if (header.version == 0x02)
                return read_frame<true>(metadata, frame_stream, uses_unsynchronization);
            return read_frame<false>(metadata, frame_stream, uses_unsynchronization);
        }();
        if (result.is_error()) {
            dbgln("Failed to parse ID3 frame: {}", result.error());
            break;
        }
        if (frame_stream.is_eof())
            break;

        if (has_flag(header.flags, ID3v2Flags::FooterPresent)) {
            if (data.bytes().slice(frame_stream.offset()).starts_with("3DI"sv.bytes()))
                break;
        } else {
            if (data[frame_stream.offset()] == '\0')
                break;
        }
    }

    return metadata;
}

// 4. ID3v2 frame overview, ID3 tag version 2.4.0 - Main Structure
enum class TextEncoding : u8 {
    ISO_8859_1 = 0,
    UTF_16_BOM = 1,
    UTF_16BE = 2,
    UTF_8 = 3,
};

[[maybe_unused]] static StringView text_encoding_to_string(TextEncoding encoding)
{
    switch (encoding) {
    case TextEncoding::ISO_8859_1:
        return "ISO-8859-1"sv;
    case TextEncoding::UTF_16_BOM:
        return "UTF-16"sv;
    case TextEncoding::UTF_16BE:
        return "UTF-16BE"sv;
    case TextEncoding::UTF_8:
        return "UTF-8"sv;
    }
    return "unknown encoding"sv;
}

static ErrorOr<String> decode_id3_string(TextEncoding encoding, StringView bytes)
{
    switch (encoding) {
    case TextEncoding::ISO_8859_1:
    case TextEncoding::UTF_8:
        return String::from_utf8(bytes);
    case TextEncoding::UTF_16_BOM:
        if (auto maybe_decoder = TextCodec::bom_sniff_to_decoder(bytes); maybe_decoder.has_value())
            return maybe_decoder->to_utf8(bytes);
        return Error::from_string_literal("Missing or invalid BOM");
    case TextEncoding::UTF_16BE:
        return TextCodec::decoder_for("utf-16be"sv)->to_utf8(bytes);
    }
    return Error::from_string_literal("Unknown text encoding type");
}

template<bool IsLegacyID3v2_0>
struct FrameHeader {
    Array<u8, IsLegacyID3v2_0 ? 3 : 4> frame_id;
    u32 size;
    u8 status_flags;
    FormatFlags format_flags;
};

template<bool IsLegacyID3v2_0>
static ErrorOr<FrameHeader<IsLegacyID3v2_0>> read_frame_header(Stream& stream)
{
    if constexpr (IsLegacyID3v2_0) {
        return FrameHeader<IsLegacyID3v2_0> {
            .frame_id = TRY((stream.read_value<Array<u8, 3>>())),
            .size = TRY(read_u24(stream)),

            // These values don't exist in the older version.
            .status_flags = 0,
            .format_flags = {},
        };
    } else {
        return FrameHeader<IsLegacyID3v2_0> {
            .frame_id = TRY((stream.read_value<Array<u8, 4>>())),
            .size = TRY(read_synchsafe_u32(stream)),
            .status_flags = TRY((stream.read_value<u8>())),
            .format_flags = TRY((stream.read_value<FormatFlags>())),
        };
    }
}

template<bool IsLegacyID3v2_0>
constexpr static StringView translated_frame_name(StringView frame_name)
{
    if (!IsLegacyID3v2_0)
        return frame_name;

    // 4.2. Text information frames
    if (frame_name == "TIT2"sv)
        return "TT2"sv;
    if (frame_name == "TIT3"sv)
        return "TT3"sv;
    if (frame_name == "TALB"sv)
        return "TAL"sv;
    if (frame_name == "TRCK"sv)
        return "TRK"sv;
    if (frame_name == "TSRC"sv)
        return "TRC"sv;

    // 4.2.2. Involved persons frames
    if (frame_name == "TPE1"sv)
        return "TP1"sv;
    if (frame_name == "TPE3"sv)
        return "TP3"sv;
    if (frame_name == "TOLY"sv)
        return "TXT"sv;
    if (frame_name == "TCOM"sv)
        return "TCM"sv;

    // 4.2.3. Derived and subjective properties frames
    if (frame_name == "TBPM"sv)
        return "TBP"sv;

    // 4.2.4 Rights and license frames
    if (frame_name == "TCOP"sv)
        return "TCR"sv;

    VERIFY_NOT_REACHED();
}

template<bool IsLegacyID3v2_0>
static ErrorOr<void> read_frame(Metadata& metadata_to_write_into, Stream& stream, bool uses_unsynchronization)
{
    auto header = TRY(read_frame_header<IsLegacyID3v2_0>(stream));
    StringView frame_name(header.frame_id);

    dbgln_if(AID3_DEBUG, "ID3 frame {}, size: {}", frame_name, header.size);
    if constexpr (!IsLegacyID3v2_0) {
        Vector<StringView> active_flags;
        if (has_flag(header.format_flags, FormatFlags::GroupedIdentity))
            active_flags.append("GroupedIdentity"sv);
        if (has_flag(header.format_flags, FormatFlags::Compressed))
            active_flags.append("Compressed"sv);
        if (has_flag(header.format_flags, FormatFlags::Encrypted))
            active_flags.append("Encrypted"sv);
        if (has_flag(header.format_flags, FormatFlags::Unsynchronizated))
            active_flags.append("Unsynchronizated"sv);
        dbgln_if(AID3_DEBUG, "  active flags: {}", active_flags);
    }

    auto buffer = TRY(ByteBuffer::create_uninitialized(header.size));
    TRY(stream.read_until_filled(buffer));

    if (uses_unsynchronization && has_flag(header.format_flags, FormatFlags::Unsynchronizated)) {
        for (size_t i = 0; i < (buffer.size() - 1); ++i) {
            if (buffer[i] == 0xFF && buffer[i + 1] == 0x00) {
                buffer.bytes().slice(i + 2).copy_to(buffer.bytes().slice(i + 1));
                buffer.resize(buffer.size() - 1);
            }
        }
    }

    if (has_flag(header.format_flags, FormatFlags::Compressed)) {
        auto frame_stream = make<FixedMemoryStream>(buffer.bytes());
        auto stream = TRY(Compress::ZlibDecompressor::create(move(frame_stream)));
        buffer = TRY(stream->read_until_eof());
    }

    // 4.2. Text information frames
    if (frame_name[0] == 'T') {
        auto encoding = static_cast<TextEncoding>(buffer[0]);
        String decoded_contents = TRY(decode_id3_string(encoding, { buffer.bytes().slice(1) }));

        // All text information frames supports multiple strings, stored as a null separated list,
        // where null is reperesented by the termination code for the charater encoding.
        // - 4.2. Text information frames, ID3 tag version 2.4.0 - Native Frames
        auto contents_list = TRY(decoded_contents.split('\0', SplitBehavior::Nothing));

        dbgln_if(AID3_DEBUG, "  encoding: {}", text_encoding_to_string(encoding));
        dbgln_if(AID3_DEBUG, "  value: {}", contents_list);

        auto add_metadata = [&metadata_to_write_into, &contents_list, &frame_name](auto& metadata_element) -> ErrorOr<void> {
            for (auto& content : contents_list) {
                if (metadata_element.has_value())
                    TRY(metadata_to_write_into.add_miscellaneous(MUST(String::from_utf8(frame_name)), move(content)));
                else
                    metadata_element = content;
            }
            return {};
        };

        auto add_people = [&metadata_to_write_into, &contents_list](Person::Role role) -> ErrorOr<void> {
            for (auto& content : contents_list)
                TRY(metadata_to_write_into.add_person(role, move(content)));
            return {};
        };

        // 4.2.1. Identification frames
        if (frame_name == translated_frame_name<IsLegacyID3v2_0>("TIT2"sv)) {
            TRY(add_metadata(metadata_to_write_into.title));
        } else if (frame_name == translated_frame_name<IsLegacyID3v2_0>("TIT3"sv)) {
            TRY(add_metadata(metadata_to_write_into.subtitle));
        } else if (frame_name == translated_frame_name<IsLegacyID3v2_0>("TALB"sv)) {
            TRY(add_metadata(metadata_to_write_into.album));
        } else if (frame_name == translated_frame_name<IsLegacyID3v2_0>("TRCK"sv)) {
            for (auto const& content : contents_list) {
                auto track_number = content.bytes_as_string_view().find_first_split_view('/').to_uint();
                if (metadata_to_write_into.track_number.has_value() || !track_number.has_value())
                    TRY(metadata_to_write_into.add_miscellaneous(MUST(String::from_utf8(frame_name)), content));
                else
                    metadata_to_write_into.track_number = track_number;
            }
        } else if (frame_name == translated_frame_name<IsLegacyID3v2_0>("TSRC"sv)) {
            TRY(add_metadata(metadata_to_write_into.isrc));
        }

        // 4.2.2. Involved persons frames
        else if (frame_name == translated_frame_name<IsLegacyID3v2_0>("TPE1"sv)) {
            TRY(add_people(Person::Role::Artist));
        } else if (frame_name == translated_frame_name<IsLegacyID3v2_0>("TPE3"sv)) {
            TRY(add_people(Person::Role::Conductor));
        } else if (frame_name == translated_frame_name<IsLegacyID3v2_0>("TOLY"sv)) {
            TRY(add_people(Person::Role::Lyricist));
        } else if (frame_name == translated_frame_name<IsLegacyID3v2_0>("TCOM"sv)) {
            TRY(add_people(Person::Role::Composer));
        }

        // 4.2.3. Derived and subjective properties frames
        else if (frame_name == translated_frame_name<IsLegacyID3v2_0>("TBPM"sv)) {
            for (auto const& content : contents_list) {
                auto bpm = content.template to_number<float>();
                if (metadata_to_write_into.bpm.has_value() || !bpm.has_value())
                    TRY(metadata_to_write_into.add_miscellaneous(MUST(String::from_utf8(frame_name)), content));
                else
                    metadata_to_write_into.bpm = bpm;
            }
        }

        // 4.2.4 Rights and license frames
        else if (frame_name == translated_frame_name<IsLegacyID3v2_0>("TCOP"sv)) {
            for (auto const& content : contents_list) {
                if (metadata_to_write_into.copyright.has_value())
                    TRY(metadata_to_write_into.add_miscellaneous(MUST(String::from_utf8(frame_name)), content));
                else
                    metadata_to_write_into.copyright = content;
            }
        }
    }

    return {};
}

}
