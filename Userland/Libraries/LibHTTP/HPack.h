/*
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <AK/Vector.h>

namespace HTTP {

struct HPackHeader {
    String name;
    String value;
};

namespace Details {

class DynamicTable final {
public:
    DynamicTable(size_t max_size)
        : m_max_size(max_size)
    {
    }

    HPackHeader const& operator[](size_t i) const { return m_table[i]; }
    size_t element_count() const { return m_table.size(); }

    size_t table_size() const;
    void resize(u32 new_byte_size);
    void insert(HPackHeader&& entry);

private:
    static size_t entry_size(HPackHeader const& entry);

    Vector<HPackHeader> m_table;
    size_t m_max_size;
};

}

class HPack final {
public:
    HPack(Vector<HPackHeader> static_table, u32 max_dynamic_table_size)
        : m_static_table(move(static_table))
        , m_dynamic_table(max_dynamic_table_size)
        , m_protocol_max_size(max_dynamic_table_size)
    {
    }

    static HPack create_with_http2_table(u32 max_dynamic_table_size);

    ErrorOr<void> encode(Stream& stream, Span<HPackHeader> headers);
    ErrorOr<Vector<HPackHeader>> decode(NonnullOwnPtr<Stream> stream);

private:
    ErrorOr<HPackHeader> table_at(u32 index) const;

    Vector<HPackHeader> m_static_table;
    Details::DynamicTable m_dynamic_table;
    u32 m_protocol_max_size;
};

}
