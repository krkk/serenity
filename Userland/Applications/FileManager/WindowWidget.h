/*
 * Copyright (c) 2025, Karol Kosek <krkk@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include "DirectoryView.h"
#include <LibGUI/Widget.h>
#include <LibGUI/TreeView.h>
#include <LibGUI/ActionGroup.h>

namespace FileManager {

class WindowWidget final : public GUI::Widget {
    C_OBJECT_ABSTRACT(ArchiveTab)

public:
    virtual ~WindowWidget() override = default;
    static ErrorOr<NonnullRefPtr<WindowWidget>> try_create();
    ErrorOr<void> initialize();

    ErrorOr<void> initialize_menubar(GUI::Window&);
    void open(ByteString const& initial_location, ByteString const& entry_focused_on_init);

private:
    WindowWidget() = default;

    ErrorOr<void> setup_actions();
    void create_toolbar();
    void refresh_tree_view();
    
    auto tree_view_selected_file_paths() const {
        Vector<ByteString> paths;
        m_tree_view->selection().for_each_index([&](GUI::ModelIndex const& index) {
            paths.append(m_directories_model->full_path(index));
        });
        return paths;
    }

    auto show_dotfiles_in_view(bool show_dotfiles) {
        m_directory_view->set_should_show_dotfiles(show_dotfiles);
        m_directories_model->set_should_show_dotfiles(show_dotfiles);
    };

    RefPtr<GUI::ToolbarContainer> m_toolbar_container;
    RefPtr<GUI::Toolbar> m_main_toolbar;
    RefPtr<GUI::Toolbar> m_breadcrumb_toolbar;
    RefPtr<GUI::PathBreadcrumbbar> m_breadcrumbbar;

    RefPtr<GUI::TreeView> m_tree_view;
    RefPtr<GUI::Menu> m_tree_view_directory_context_menu;
    RefPtr<GUI::Action> m_tree_view_open_in_new_window_action;
    RefPtr<GUI::Action> m_tree_view_open_in_new_terminal_action;
    RefPtr<GUI::Action> m_tree_view_delete_action;

    RefPtr<DirectoryView> m_directory_view;
    RefPtr<GUI::FileSystemModel> m_directories_model;

    RefPtr<GUI::Action> m_directory_open_action;
    RefPtr<GUI::Action> m_shortcut_action;
    RefPtr<GUI::Action> m_create_archive_action;
    RefPtr<GUI::Action> m_show_dotfiles_action;
    RefPtr<GUI::Action> m_set_wallpaper_action;
    RefPtr<GUI::Action> m_unzip_archive_action;

    RefPtr<GUI::Action> m_new_window_action;
    RefPtr<GUI::Action> m_mkdir_action;
    RefPtr<GUI::Action> m_focus_dependent_delete_action;

    RefPtr<GUI::Action> m_cut_action;
    RefPtr<GUI::Action> m_copy_action;
    RefPtr<GUI::Action> m_copy_path_action;
    RefPtr<GUI::Action> m_paste_action;
    RefPtr<GUI::Action> m_select_all_action;

    RefPtr<GUI::Action> m_go_back_action;
    RefPtr<GUI::Action> m_go_forward_action;
    RefPtr<GUI::Action> m_open_parent_directory_action;
    RefPtr<GUI::Action> m_open_child_directory_action;
    RefPtr<GUI::Action> m_go_home_action;
    RefPtr<GUI::Action> m_go_to_location_action;

    RefPtr<GUI::Action> m_properties_action;
    RefPtr<GUI::Action> m_folder_specific_paste_action;

    RefPtr<GUI::Action> m_layout_toolbar_action;
    RefPtr<GUI::Action> m_layout_location_action;
    RefPtr<GUI::Action> m_layout_statusbar_action;
    RefPtr<GUI::Action> m_layout_folderpane_action;

    OwnPtr<GUI::ActionGroup> m_view_type_action_group;


    RefPtr<GUI::Menu> m_file_context_menu;
    Vector<NonnullRefPtr<LauncherHandler>> m_current_file_handlers;
    RefPtr<GUI::Action> m_file_context_menu_action_default_action;

    bool m_show_toolbar;
    bool m_show_location;
    bool m_is_reacting_to_tree_view_selection_change = false;
};

}