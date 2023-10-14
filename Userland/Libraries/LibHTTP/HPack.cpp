/*
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/BitStream.h>
#include <AK/MemoryStream.h>
#include <AK/UFixedBigInt.h>
#include <LibHTTP/HPack.h>
#include <LibHTTP/HPackHuffmanTables.h>

namespace HTTP::HPack {

static ErrorOr<u32> decode_hpack_integer(BigEndianInputBitStream& stream, u8 prefix_count)
{
    // 5.1. Integer Representation
    Checked<u32> value = TRY(stream.read_bits(prefix_count));
    VERIFY(stream.is_aligned_to_byte_boundary());
    if ((1 << prefix_count) - 1 != value.value())
        return value.value_unchecked();

    // NOTE: It's a little endian encoded number with a twist that the largest bit in an octet continues reading data.
    u8 power = 0;
    while (true) {
        auto octet = TRY(stream.read_value<u8>());
        bool is_last = (octet & 0b1000'0000) == 0;

        value += (octet & 0b0111'1111) * (1 << power);
        power += 7;

        if (is_last) {
            // Integer encodings that exceed implementation limits — in value or octet length — MUST be treated as decoding errors.
            if (value.has_overflow())
                return Error::from_string_literal("HPack integer exceeded u32 size");
            return value.value();
        }
    }
}

static ErrorOr<String> decode_hpack_string(BigEndianInputBitStream& stream)
{
    // 5.2. String Literal Representation
    VERIFY(stream.is_aligned_to_byte_boundary());

    bool huffman_encoded = TRY(stream.read_bit());
    auto length = TRY(decode_hpack_integer(stream, 7));

    auto string_data = TRY(ByteBuffer::create_uninitialized(length));
    TRY(stream.read_until_filled(string_data));

    if (huffman_encoded) {
        auto huffman_stream = make<FixedMemoryStream>(string_data.bytes());
        BigEndianInputBitStream huffman_bitstream(move(huffman_stream));

        auto bit_length = (u256)length * 8;
        StringBuilder decoded;
        while (true) {
            auto result = huffman_decode(huffman_bitstream, Tree.span(), bit_length > 30 ? 30 : (size_t)bit_length);
            bit_length -= result.bits_read;
            // Upon decoding, an incomplete code at the end of the encoded data is to be considered as padding and discarded.
            if (!result.code.has_value() && bit_length == 0)
                break;
            if (!result.code.has_value() || result.code->symbol.symbol == 256)
                return Error::from_string_literal("error decoding huffman");
            TRY(decoded.try_append(static_cast<char>(result.code->symbol.symbol)));
        }
        return decoded.to_string();
    }

    return String::from_utf8(string_data);
}

Decoder Decoder::create_with_http2_table(u32 max_dynamic_table_size)
{
    // clang-format off
    // https://httpwg.org/specs/rfc7541.html#static.table.entries
    Vector<Header> http2_table {
        { ":authority"_string,                  {} },
        { ":method"_string,                     "GET"_string },
        { ":method"_string,                     "POST"_string },
        { ":path"_string,                       "/"_string },
        { ":path"_string,                       "/index.html"_string },
        { ":scheme"_string,                     "http"_string },
        { ":scheme"_string,                     "https"_string },
        { ":status"_string,                     "200"_string },
        { ":status"_string,                     "204"_string },
        { ":status"_string,                     "206"_string },
        { ":status"_string,                     "304"_string },
        { ":status"_string,                     "400"_string },
        { ":status"_string,                     "404"_string },
        { ":status"_string,                     "500"_string },
        { "accept-charset"_string,              {} },
        { "accept-encoding"_string,             "gzip, deflate"_string },
        { "accept-language"_string,             {} },
        { "accept-ranges"_string,               {} },
        { "accept"_string,                      {} },
        { "access-control-allow-origin"_string, {} },
        { "age"_string,                         {} },
        { "allow"_string,                       {} },
        { "authorization"_string,               {} },
        { "cache-control"_string,               {} },
        { "content-disposition"_string,         {} },
        { "content-encoding"_string,            {} },
        { "content-language"_string,            {} },
        { "content-length"_string,              {} },
        { "content-location"_string,            {} },
        { "content-range"_string,               {} },
        { "content-type"_string,                {} },
        { "cookie"_string,                      {} },
        { "date"_string,                        {} },
        { "etag"_string,                        {} },
        { "expect"_string,                      {} },
        { "expires"_string,                     {} },
        { "from"_string,                        {} },
        { "host"_string,                        {} },
        { "if-match"_string,                    {} },
        { "if-modified-since"_string,           {} },
        { "if-none-match"_string,               {} },
        { "if-range"_string,                    {} },
        { "if-unmodified-since"_string,         {} },
        { "last-modified"_string,               {} },
        { "link"_string,                        {} },
        { "location"_string,                    {} },
        { "max-forwards"_string,                {} },
        { "proxy-authenticate"_string,          {} },
        { "proxy-authorization"_string,         {} },
        { "range"_string,                       {} },
        { "referer"_string,                     {} },
        { "refresh"_string,                     {} },
        { "retry-after"_string,                 {} },
        { "server"_string,                      {} },
        { "set-cookie"_string,                  {} },
        { "strict-transport-security"_string,   {} },
        { "transfer-encoding"_string,           {} },
        { "user-agent"_string,                  {} },
        { "vary"_string,                        {} },
        { "via"_string,                         {} },
        { "www-authenticate"_string,            {} },
    };
    // clang-format on

    return Decoder(move(http2_table), max_dynamic_table_size);
}

ErrorOr<Header> Decoder::table_at(u32 index) const
{
    // 2.3.3. Index Address Space
    VERIFY(index != 0);

    // Indices between 1 and the length of the static table (inclusive) refer to elements in the static table (see Section 2.3.1).
    --index;
    if (index < m_static_table.size())
        return m_static_table[index];

    // Indices strictly greater than the length of the static table refer to elements in the dynamic table
    // (see Section 2.3.2). The length of the static table is subtracted to find the index into the dynamic table.
    index -= m_static_table.size();
    if (index < m_dynamic_table.element_count())
        return m_dynamic_table[index];

    // Indices strictly greater than the sum of the lengths of both tables MUST be treated as a decoding error.
    return Error::from_string_literal("invalid index");
}

ErrorOr<Vector<Header>> Decoder::decode(NonnullOwnPtr<Stream> stream)
{
    BigEndianInputBitStream bit_stream(move(stream));

    Vector<Header> headers;

    while (!bit_stream.is_eof()) {
        // 6.1. Indexed Header Field Representation
        // 0b1--- ----
        bool is_indexed_representation = TRY(bit_stream.read_bit());
        if (is_indexed_representation) {
            u8 index = TRY(decode_hpack_integer(bit_stream, 7));
            // The index value of 0 is not used. It MUST be treated as a decoding error if found in an indexed header field representation.
            if (index == 0)
                return Error::from_string_literal("index 0");
            auto header = TRY(table_at(index));
            headers.append({ header.name, header.value });
            continue;
        }

        // 6.2.1. Literal Header Field with Incremental Indexing
        // 0b01-- ----
        bool is_literal_header_with_indexing = TRY(bit_stream.read_bit());
        if (is_literal_header_with_indexing) {
            u8 index = TRY(decode_hpack_integer(bit_stream, 6));
            bool has_indexed_name = index != 0;
            if (has_indexed_name) {
                auto name = TRY(table_at(index)).name;
                auto value = TRY(decode_hpack_string(bit_stream));
                headers.append({ name, value });
                m_dynamic_table.insert({ name, value });
            } else {
                auto name = TRY(decode_hpack_string(bit_stream));
                auto value = TRY(decode_hpack_string(bit_stream));
                headers.append({ name, value });
                m_dynamic_table.insert({ name, value });
            }
            continue;
        }

        // 6.3. Dynamic Table Size Update
        // 0b001- ----
        bool is_dynamic_table_size_update = TRY(bit_stream.read_bit());
        if (is_dynamic_table_size_update) {
            auto new_size = TRY(decode_hpack_integer(bit_stream, 5));

            // The new maximum size MUST be lower than or equal to the limit determined by the protocol using HPACK.
            // A value that exceeds this limit MUST be treated as a decoding error.
            if (new_size > m_protocol_max_size)
                return Error::from_string_literal("Dynamic Table Size Update value exceeded the limit");

            m_dynamic_table.resize(new_size);
            continue;
        }

        // 6.2.2. Literal Header Field without Indexing
        // 0b0000 ----
        bool fourth_bit_set = TRY(bit_stream.read_bit());
        if (!fourth_bit_set) {
            auto index = TRY(decode_hpack_integer(bit_stream, 4));
            bool has_indexed_name = index != 0;
            if (has_indexed_name) {
                auto name = TRY(table_at(index)).name;
                auto value = TRY(decode_hpack_string(bit_stream));
                headers.append({ name, value });
            } else {
                auto name = TRY(decode_hpack_string(bit_stream));
                auto value = TRY(decode_hpack_string(bit_stream));
                headers.append({ name, value });
            }
        }
        // 6.2.3. Literal Header Field Never Indexed
        // 0b0001 ----
        else {
            auto index = TRY(decode_hpack_integer(bit_stream, 4));
            bool has_indexed_name = index != 0;
            if (has_indexed_name) {
                auto name = TRY(table_at(index)).name;
                auto value = TRY(decode_hpack_string(bit_stream));
                headers.append({ name, value });
            } else {
                auto name = TRY(decode_hpack_string(bit_stream));
                auto value = TRY(decode_hpack_string(bit_stream));
                headers.append({ name, value });
            }
        }
    }

    return headers;
}

size_t Details::DynamicTable::table_size() const
{
    // 4.1. Calculating Table Size
    //      The size of the dynamic table is the sum of the size of its entries.
    size_t size = 0;
    for (size_t i = 0; i < m_table.size(); ++i)
        size += entry_size(m_table[i]);
    return size;
}

void Details::DynamicTable::resize(u32 new_byte_size)
{
    // 4.3. Entry Eviction When Dynamic Table Size Changes
    //      Whenever the maximum size for the dynamic table is reduced, entries are evicted from the end of
    //      the dynamic table until the size of the dynamic table is less than or equal to the maximum size.
    auto size = this->table_size();
    while (size > new_byte_size) {
        auto oldest_element = m_table.take_last();
        size -= entry_size(oldest_element);
    }
    m_max_size = new_byte_size;
}

void Details::DynamicTable::insert(Header&& entry)
{
    // 4.4. Entry Eviction When Adding New Entries
    //      Before a new entry is added to the dynamic table, entries are evicted from the end of the dynamic table
    //      until the size of the dynamic table is less than or equal to (maximum size - new entry size)
    //      or until the table is empty.
    auto size = this->table_size();
    while (size > m_max_size - entry_size(entry) && !m_table.is_empty()) {
        auto oldest_element = m_table.take_last();
        size -= entry_size(oldest_element);
    }

    //      If the size of the new entry is less than or equal to the maximum size, that entry is added to the table.
    //      It is not an error to attempt to add an entry that is larger than the maximum size; an attempt to add an
    //      entry larger than the maximum size causes the table to be emptied of all existing entries and results in
    //      an empty table.
    // FIXME: Prepends might be slow.
    if (entry_size(entry) <= m_max_size)
        m_table.prepend(move(entry));
}

size_t Details::DynamicTable::entry_size(Header const& entry)
{
    // 4.1. Calculating Table Size
    //      [...]
    //      The size of an entry is the sum of its name's length in octets (as defined in Section 5.2),
    //      its value's length in octets, and 32.
    return entry.name.bytes().size() + entry.value.bytes().size() + 32;
}

}
