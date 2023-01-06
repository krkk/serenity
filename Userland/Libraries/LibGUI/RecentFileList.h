/*
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/LexicalPath.h>
#include <LibCore/DateTime.h>

namespace GUI {

class RecentFile {
public:
    static ErrorOr<String> history_path();
    static ErrorOr<Vector<RecentFile>> read_history();
    static ErrorOr<void> write_to_history(StringView path, Core::DateTime = Core::DateTime::now());

    DeprecatedString const& full_path() const { return m_path.string(); }
    StringView basename() const { return m_path.basename(); }
    Core::DateTime const& access_time() const { return m_access_time; }
    bool exists() const { return m_size.has_value(); }
    Optional<size_t> size() const { return m_size; }

private:
    RecentFile(Core::DateTime access_time, LexicalPath path, Optional<size_t> size)
        : m_access_time(access_time)
        , m_path(move(path))
        , m_size(move(size))
    {
    }

    Core::DateTime m_access_time;
    LexicalPath m_path;
    Optional<size_t> m_size;
};

}
