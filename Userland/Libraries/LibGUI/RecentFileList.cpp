/*
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/String.h>
#include <AK/URL.h>
#include <LibCore/MappedFile.h>
#include <LibCore/StandardPaths.h>
#include <LibCore/Stream.h>
#include <LibCore/System.h>
#include <LibGUI/RecentFileList.h>

namespace GUI {

constexpr auto DateFormat = "%Y-%m-%d %H:%M:%S"sv;

ErrorOr<String> RecentFile::history_path()
{
    // FIXME: Use XDG_STATE_HOME ($HOME/.local/state)?
    auto path = LexicalPath::join(Core::StandardPaths::config_directory(), "RecentFiles.csv"sv);
    return String::from_deprecated_string(path.string());
}

ErrorOr<Vector<RecentFile>> RecentFile::read_history()
{
    auto mapped_file_or_error = Core::MappedFile::map(TRY(history_path()));
    if (mapped_file_or_error.is_error()) {
        if (mapped_file_or_error.error().code() == ENOENT)
            return Vector<RecentFile>();
        return mapped_file_or_error.error();
    }
    auto lines = StringView(mapped_file_or_error.value()->bytes()).lines();

    Vector<RecentFile> files;
    for (auto line : lines.in_reverse()) {
        auto tabs = line.split_view('\t');
        if (tabs.size() < 2)
            continue;
        auto date = Core::DateTime::parse(DateFormat, tabs[0]);
        if (!date.has_value())
            continue;
        auto path = URL::percent_decode(tabs[1]);
        auto file_already_on_list = !files.find_if([&](RecentFile& file) { return file.full_path() == path; }).is_end();
        if (file_already_on_list)
            continue;
        auto size = [path]() -> Optional<size_t> {
            auto stat_or_error = Core::System::stat(path);
            if (stat_or_error.is_error())
                return {};
            return stat_or_error.value().st_size;
        }();
        TRY(files.try_append(RecentFile(*date, LexicalPath(path), size)));
    }
    return files;
}

ErrorOr<void> RecentFile::write_to_history(StringView path, Core::DateTime date_time)
{
    auto history_path = TRY(GUI::RecentFile::history_path());
    auto file = TRY(Core::Stream::File::open(history_path, Core::Stream::OpenMode::Write | Core::Stream::OpenMode::Append));

    auto line = TRY(String::formatted("{}\t{}\n", date_time.to_deprecated_string(DateFormat), URL::percent_encode(path, URL::PercentEncodeSet::C0Control)));
    TRY(file->write_entire_buffer(line.bytes()));
    return {};
}

}
