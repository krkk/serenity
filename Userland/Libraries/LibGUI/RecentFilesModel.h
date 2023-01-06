/*
 * Copyright (c) 2023, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibCore/FileWatcher.h>
#include <LibGUI/Model.h>

namespace GUI {

class RecentFile;

class RecentFilesModel : public Model {
public:
    enum class CustomRole {
        _DONOTUSE = (int)ModelRole::Custom,
        FullPath,
    };

    enum Column {
        Icon = 0,
        Name,
        Size,
        AccessTime,
        __Count,
    };

    static ErrorOr<NonnullRefPtr<RecentFilesModel>> create();
    virtual ~RecentFilesModel() override = default;

    RecentFile const& node(ModelIndex const& index) const;

    virtual int row_count(ModelIndex const& = ModelIndex()) const override;
    virtual int column_count(ModelIndex const& = ModelIndex()) const override { return Column::__Count; }
    virtual DeprecatedString column_name(int column) const override;
    virtual Variant data(ModelIndex const&, ModelRole = ModelRole::Display) const override;
    virtual bool is_column_sortable(int column_index) const override { return column_index != Column::Icon; }
    virtual bool is_searchable() const override { return true; }
    virtual Vector<ModelIndex> matches(StringView, unsigned = MatchesFlag::AllMatching, ModelIndex const& = ModelIndex()) override;
    virtual void invalidate() override;

private:
    ErrorOr<void> read_history();

    Optional<RecentFile const&> node_for_path(DeprecatedString const&) const;
    static GUI::Icon icon_for(DeprecatedString const& path);
    void handle_file_event(Core::FileWatcherEvent const& event);

    Vector<RecentFile> m_nodes;
    RefPtr<Core::FileWatcher> m_file_watcher;
};

}
