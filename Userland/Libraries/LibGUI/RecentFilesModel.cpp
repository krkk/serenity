/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, sin-ack <sin-ack@protonmail.com>
 * Copyright (c) 2022, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/NumberFormat.h>
#include <AK/String.h>
#include <LibGUI/FileIconProvider.h>
#include <LibGUI/RecentFileList.h>
#include <LibGUI/RecentFilesModel.h>

namespace GUI {

ErrorOr<NonnullRefPtr<RecentFilesModel>> RecentFilesModel::create()
{
    auto model = TRY(try_make_ref_counted<RecentFilesModel>());

    auto setup_watcher = [&model]() -> ErrorOr<void> {
        auto watcher = TRY(Core::FileWatcher::create());
        auto path = TRY(RecentFile::history_path());
        TRY(watcher->add_watch(path.to_deprecated_string(), Core::FileWatcherEvent::Type::ContentModified));
        watcher->on_change = [model = model.ptr()](Core::FileWatcherEvent const& event) {
            model->handle_file_event(event);
        };
        model->m_file_watcher = move(watcher);
        return {};
    };
    if (auto maybe_error = setup_watcher(); maybe_error.is_error())
        dbgln("Couldn't setup file watcher: {}", maybe_error.error());

    model->invalidate();
    return model;
}

void RecentFilesModel::invalidate()
{
    auto history_or_error = RecentFile::read_history();
    if (history_or_error.is_error())
        dbgln("Couldn't read history: {}", history_or_error.error());
    else
        m_nodes = history_or_error.release_value();

    Model::invalidate();
}

void RecentFilesModel::handle_file_event(Core::FileWatcherEvent const& event)
{
    switch (event.type) {
    case Core::FileWatcherEvent::Type::ContentModified: {
        invalidate();
        break;
    }
    default:
        VERIFY_NOT_REACHED();
    }

    did_update(UpdateFlag::DontInvalidateIndices);
}

int RecentFilesModel::row_count(ModelIndex const& index) const
{
    if (!index.is_valid())
        return m_nodes.size();
    return 0;
}

RecentFile const& RecentFilesModel::node(ModelIndex const& index) const
{
    return m_nodes[index.row()];
}

Variant RecentFilesModel::data(ModelIndex const& index, ModelRole role) const
{
    VERIFY(index.is_valid());

    if (role == ModelRole::TextAlignment) {
        switch (index.column()) {
        case Column::Icon:
            return Gfx::TextAlignment::Center;
        case Column::Size:
            return Gfx::TextAlignment::CenterRight;
        case Column::Name:
        case Column::AccessTime:
            return Gfx::TextAlignment::CenterLeft;
        }
        VERIFY_NOT_REACHED();
    }

    auto const& node = this->node(index);

    if (role == static_cast<GUI::ModelRole>(CustomRole::FullPath)) {
        // For GUI::RecentFilesModel, custom role means the full path.
        VERIFY(index.column() == Column::Name);
        return node.full_path();
    }

    if (role == ModelRole::MimeData) {
        if (index.column() == Column::Name)
            return URL::create_with_file_scheme(node.full_path()).serialize();
        return {};
    }

    if (role == ModelRole::Sort) {
        switch (index.column()) {
        case Column::Icon:
            return {};
        case Column::Name:
            return node.basename();
        case Column::Size:
            return node.size().value_or(0);
        case Column::AccessTime:
            return node.access_time().timestamp();
        }
        VERIFY_NOT_REACHED();
    }

    if (role == ModelRole::Display) {
        switch (index.column()) {
        case Column::Icon:
            return icon_for(node.full_path());
        case Column::Name:
            return node.basename();
        case Column::Size:
            if (!node.size().has_value())
                return "";
            return human_readable_size(*node.size());
        case Column::AccessTime:
            return node.access_time().to_deprecated_string();
        }
        VERIFY_NOT_REACHED();
    }

    if (role == ModelRole::Icon) {
        return icon_for(node.full_path());
    }

    if (role == ModelRole::IconOpacity) {
        if (!node.exists())
            return 0.5f;
        return {};
    }

    return {};
}

Icon RecentFilesModel::icon_for(DeprecatedString const& path)
{
    // FIXME: Add thumbnails
    return FileIconProvider::icon_for_path(path, 0);
}

DeprecatedString RecentFilesModel::column_name(int column) const
{
    switch (column) {
    case Column::Icon:
        return "";
    case Column::Name:
        return "Name";
    case Column::Size:
        return "Size";
    case Column::AccessTime:
        return "Access Time";
    }
    VERIFY_NOT_REACHED();
}

Vector<ModelIndex> RecentFilesModel::matches(StringView searching, unsigned flags, ModelIndex const&)
{
    Vector<ModelIndex> found_indices;
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        auto node = *it;
        if (!string_matches(node.basename(), searching, flags))
            continue;
        found_indices.append(index(it.index()));
        if (flags & FirstMatchOnly)
            break;
    }

    return found_indices;
}

}
