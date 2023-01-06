/*
 * Copyright (c) 2018-2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Function.h>
#include <AK/LexicalPath.h>
#include <LibCore/File.h>
#include <LibCore/StandardPaths.h>
#include <LibGUI/Action.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Button.h>
#include <LibGUI/CommonLocationsProvider.h>
#include <LibGUI/FileIconProvider.h>
#include <LibGUI/FilePicker.h>
#include <LibGUI/FilePickerDialogGML.h>
#include <LibGUI/FileSystemModel.h>
#include <LibGUI/InputBox.h>
#include <LibGUI/Label.h>
#include <LibGUI/Menu.h>
#include <LibGUI/MessageBox.h>
#include <LibGUI/MultiView.h>
#include <LibGUI/RecentFileList.h>
#include <LibGUI/RecentFilesModel.h>
#include <LibGUI/SortingProxyModel.h>
#include <LibGUI/TextBox.h>
#include <LibGUI/Toolbar.h>
#include <LibGUI/Tray.h>
#include <LibGfx/Font/FontDatabase.h>
#include <LibGfx/Palette.h>
#include <string.h>
#include <unistd.h>

namespace GUI {

Optional<DeprecatedString> FilePicker::get_open_filepath(Window* parent_window, DeprecatedString const& window_title, StringView path, bool folder, ScreenPosition screen_position)
{
    auto picker = FilePicker::construct(parent_window, folder ? Mode::OpenFolder : Mode::Open, ""sv, path, screen_position);

    if (!window_title.is_null())
        picker->set_title(window_title);

    if (picker->exec() == ExecResult::OK) {
        DeprecatedString file_path = picker->selected_file();

        if (file_path.is_null())
            return {};

        return file_path;
    }
    return {};
}

Optional<DeprecatedString> FilePicker::get_save_filepath(Window* parent_window, DeprecatedString const& title, DeprecatedString const& extension, StringView path, ScreenPosition screen_position)
{
    auto picker = FilePicker::construct(parent_window, Mode::Save, DeprecatedString::formatted("{}.{}", title, extension), path, screen_position);

    if (picker->exec() == ExecResult::OK) {
        DeprecatedString file_path = picker->selected_file();

        if (file_path.is_null())
            return {};

        return file_path;
    }
    return {};
}

void FilePicker::setup_filesystem_model(DeprecatedString const& root_path)
{
    auto model = FileSystemModel::create(root_path);
    m_view->set_model(MUST(SortingProxyModel::create(model)));
    m_view->set_model_column(FileSystemModel::Column::Name);
    m_view->set_key_column_and_sort_order(GUI::FileSystemModel::Column::Name, GUI::SortOrder::Ascending);
    m_view->set_column_visible(FileSystemModel::Column::User, true);
    m_view->set_column_visible(FileSystemModel::Column::Group, true);
    m_view->set_column_visible(FileSystemModel::Column::Permissions, true);
    m_view->set_column_visible(FileSystemModel::Column::Inode, true);
    m_view->set_column_visible(FileSystemModel::Column::SymlinkTarget, true);

    model->on_complete = [this] {
        auto& model = verify_cast<FileSystemModel>(*m_model);
        m_view->set_active_widget(&m_view->current_view());
        for (auto& location_button : m_common_location_buttons)
            m_common_locations_tray->set_item_checked(location_button.tray_item_index, model.root_path() == location_button.path);

        m_view->view_as_icons_action().set_enabled(true);
        m_view->view_as_table_action().set_enabled(true);
        m_view->view_as_columns_action().set_enabled(true);
    };

    model->on_directory_change_error = [this](int, char const* error_string) {
        auto& model = verify_cast<FileSystemModel>(*m_model);
        m_error_label->set_text(DeprecatedString::formatted("Could not open {}:\n{}", model.root_path(), error_string));
        m_view->set_active_widget(m_error_label);

        m_view->view_as_icons_action().set_enabled(false);
        m_view->view_as_table_action().set_enabled(false);
        m_view->view_as_columns_action().set_enabled(false);
    };

    model->register_client(*this);

    m_view->for_each_view_implementation([](AbstractView& view) {
        view.set_editable(true);
        view.set_edit_triggers(GUI::AbstractView::EditTrigger::None);
    });

    m_open_parent_directory_action->set_enabled(true);
    m_mkdir_action->set_enabled(true);
    m_ok_button->set_enabled(m_mode == Mode::OpenFolder || !m_filename_textbox->text().is_empty());

    m_location_textbox->set_text(root_path);
    m_location_textbox->set_icon(FileIconProvider::icon_for_path(root_path).bitmap_for_size(16));

    m_model = model;
    model->on_complete();
}

ErrorOr<void> FilePicker::setup_recent_model()
{
    auto model = TRY(RecentFilesModel::create());
    m_view->set_model(TRY(SortingProxyModel::create(model)));
    m_view->set_model_column(GUI::RecentFilesModel::Column::Name);
    m_view->set_key_column_and_sort_order(GUI::RecentFilesModel::Column::AccessTime, GUI::SortOrder::Descending);
    model->register_client(*this);

    for (auto& location_button : m_common_location_buttons)
        m_common_locations_tray->set_item_checked(location_button.tray_item_index, location_button.path == "recent:");

    m_view->for_each_view_implementation([](AbstractView& view) {
        view.set_editable(false);
        view.set_edit_triggers(GUI::AbstractView::EditTrigger::None);
    });

    m_open_parent_directory_action->set_enabled(false);
    m_mkdir_action->set_enabled(false);
    m_ok_button->set_enabled(m_mode == Mode::Open && !m_filename_textbox->text().is_empty());

    m_location_textbox->set_text("recent:"sv);
    m_location_textbox->set_icon(TRY(Gfx::Bitmap::try_load_from_file("/res/icons/16x16/app-analog-clock.png"sv)));

    m_model = model;
    return {};
}

FilePicker::FilePicker(Window* parent_window, Mode mode, StringView filename, StringView path, ScreenPosition screen_position)
    : Dialog(parent_window, screen_position)
    , m_mode(mode)
{
    switch (m_mode) {
    case Mode::Open:
    case Mode::OpenMultiple:
    case Mode::OpenFolder:
        set_title("Open");
        set_icon(Gfx::Bitmap::try_load_from_file("/res/icons/16x16/open.png"sv).release_value_but_fixme_should_propagate_errors());
        break;
    case Mode::Save:
        set_title("Save as");
        set_icon(Gfx::Bitmap::try_load_from_file("/res/icons/16x16/save-as.png"sv).release_value_but_fixme_should_propagate_errors());
        break;
    }
    resize(560, 320);

    auto widget = set_main_widget<GUI::Widget>().release_value_but_fixme_should_propagate_errors();
    if (!widget->load_from_gml(file_picker_dialog_gml))
        VERIFY_NOT_REACHED();

    auto& toolbar = *widget->find_descendant_of_type_named<GUI::Toolbar>("toolbar");

    m_location_textbox = *widget->find_descendant_of_type_named<GUI::TextBox>("location_textbox");

    m_view = *widget->find_descendant_of_type_named<GUI::MultiView>("view");
    m_view->set_selection_mode(m_mode == Mode::OpenMultiple ? GUI::AbstractView::SelectionMode::MultiSelection : GUI::AbstractView::SelectionMode::SingleSelection);

    m_error_label = m_view->add<GUI::Label>();
    m_error_label->set_font(m_error_label->font().bold_variant());

    m_location_textbox->on_return_pressed = [this] {
        set_path(m_location_textbox->text());
    };

    m_open_parent_directory_action = Action::create(
        "Open parent directory", { Mod_Alt, Key_Up }, Gfx::Bitmap::try_load_from_file("/res/icons/16x16/open-parent-directory.png"sv).release_value_but_fixme_should_propagate_errors(), [this](Action const&) {
            auto& model = verify_cast<FileSystemModel>(*m_model);
            set_path(DeprecatedString::formatted("{}/..", model.root_path()));
        },
        this);
    toolbar.add_action(*m_open_parent_directory_action);

    auto go_home_action = CommonActions::make_go_home_action([this](auto&) {
        set_path(Core::StandardPaths::home_directory());
    },
        this);
    toolbar.add_action(go_home_action);
    toolbar.add_separator();

    m_mkdir_action = Action::create(
        "New directory...", { Mod_Ctrl | Mod_Shift, Key_N }, Gfx::Bitmap::try_load_from_file("/res/icons/16x16/mkdir.png"sv).release_value_but_fixme_should_propagate_errors(), [this](Action const&) {
            auto& model = verify_cast<FileSystemModel>(*m_model);
            DeprecatedString value;
            if (InputBox::show(this, value, "Enter name:"sv, "New directory"sv) == InputBox::ExecResult::OK && !value.is_empty()) {
                auto new_dir_path = LexicalPath::canonicalized_path(DeprecatedString::formatted("{}/{}", model.root_path(), value));
                int rc = mkdir(new_dir_path.characters(), 0777);
                if (rc < 0) {
                    MessageBox::show(this, DeprecatedString::formatted("mkdir(\"{}\") failed: {}", new_dir_path, strerror(errno)), "Error"sv, MessageBox::Type::Error);
                } else {
                    m_model->invalidate();
                }
            }
        },
        this);

    toolbar.add_action(*m_mkdir_action);

    toolbar.add_separator();

    toolbar.add_action(m_view->view_as_icons_action());
    toolbar.add_action(m_view->view_as_table_action());
    toolbar.add_action(m_view->view_as_columns_action());

    m_filename_textbox = *widget->find_descendant_of_type_named<GUI::TextBox>("filename_textbox");
    m_filename_textbox->set_focus(true);
    if (m_mode == Mode::Save) {
        LexicalPath lexical_filename { filename };
        m_filename_textbox->set_text(filename);

        if (auto extension = lexical_filename.extension(); !extension.is_empty()) {
            TextPosition start_of_filename { 0, 0 };
            TextPosition end_of_filename { 0, filename.length() - extension.length() - 1 };

            m_filename_textbox->set_selection({ end_of_filename, start_of_filename });
        } else {
            m_filename_textbox->select_all();
        }
    }
    m_filename_textbox->on_return_pressed = [&] {
        on_file_return();
    };

    m_context_menu = GUI::Menu::construct();
    m_context_menu->add_action(GUI::Action::create_checkable(
        "Show dotfiles", { Mod_Ctrl, Key_H }, [&](auto&) {
            // m_model->set_should_show_dotfiles(action.is_checked());
            // m_model->invalidate();
        },
        this));

    m_view->on_context_menu_request = [&](const GUI::ModelIndex& index, const GUI::ContextMenuEvent& event) {
        if (!index.is_valid()) {
            m_context_menu->popup(event.screen_position());
        }
    };

    m_ok_button = widget->find_descendant_of_type_named<GUI::Button>("ok_button");
    m_ok_button->set_text(ok_button_name(m_mode));
    m_ok_button->on_click = [this](auto) {
        on_file_return();
    };

    auto& cancel_button = *widget->find_descendant_of_type_named<GUI::Button>("cancel_button");
    cancel_button.set_text("Cancel");
    cancel_button.on_click = [this](auto) {
        done(ExecResult::Cancel);
    };

    m_filename_textbox->on_change = [&] {
        bool enable;
        if (m_mode == Mode::OpenFolder)
            enable = true;
        else if (m_filename_textbox->text().is_empty())
            enable = false;
        else if (m_mode == Mode::Save)
            enable = m_view->current_view().is_editable();
        else
            enable = true;

        m_ok_button->set_enabled(enable);
    };

    m_view->on_selection_change = [this] {
        auto index = m_view->selection().first();
        auto& filter_model = (SortingProxyModel&)*m_view->model();
        auto local_index = filter_model.map_to_source(index);
        if (!local_index.is_valid())
            return;

        if (is<FileSystemModel>(*m_model)) {
            auto& model = verify_cast<FileSystemModel>(*m_model);
            auto& node = model.node(local_index);
            auto should_open_folder = m_mode == Mode::OpenFolder;
            if (should_open_folder == node.is_directory()) {
                m_filename_textbox->set_text(node.name);
            } else if (m_mode != Mode::Save) {
                m_filename_textbox->clear();
            }
        }
        if (is<RecentFilesModel>(*m_model)) {
            auto& model = verify_cast<RecentFilesModel>(*m_model);
            auto& node = model.node(local_index);
            m_filename_textbox->set_text(node.full_path());
            return;
        }
    };

    m_view->on_activation = [this](auto& index) {
        auto& filter_model = (SortingProxyModel&)*m_view->model();
        auto local_index = filter_model.map_to_source(index);
        if (is<FileSystemModel>(*m_model)) {
            auto& model = verify_cast<FileSystemModel>(*m_model);
            auto& node = model.node(local_index);
            auto path = node.full_path();

            if (node.is_directory() || node.is_symlink_to_directory()) {
                set_path(path);
                // NOTE: 'node' is invalid from here on
            } else {
                on_file_return();
            }
            return;
        }
        if (is<RecentFilesModel>(*m_model)) {
            on_file_return();
            return;
        }
        VERIFY_NOT_REACHED();
    };

    m_common_locations_tray = widget->find_descendant_of_type_named<GUI::Tray>("common_locations_tray");
    m_common_locations_tray->on_item_activation = [this](DeprecatedString const& path) {
        set_path(path);
    };
    auto recent_index = m_common_locations_tray->add_item("Recent", Gfx::Bitmap::try_load_from_file("/res/icons/16x16/app-analog-clock.png"sv).release_value_but_fixme_should_propagate_errors(), "recent:");
    m_common_location_buttons.append({ "recent:", recent_index });
    for (auto& location : CommonLocationsProvider::common_locations()) {
        auto index = m_common_locations_tray->add_item(location.name, FileIconProvider::icon_for_path(location.path).bitmap_for_size(16), location.path);
        m_common_location_buttons.append({ location.path, index });
    }

    setup_filesystem_model(path);
}

FilePicker::~FilePicker()
{
    m_model->unregister_client(*this);
}

void FilePicker::model_did_update(unsigned)
{
    if (!is<FileSystemModel>(*m_model))
        return;
    m_location_textbox->set_text(verify_cast<FileSystemModel>(*m_model).root_path());
}

void FilePicker::on_file_return()
{
    auto path = m_filename_textbox->text();
    if (!path.starts_with('/') && is<FileSystemModel>(*m_model)) {
        auto& model = verify_cast<FileSystemModel>(*m_model);
        path = LexicalPath::join(model.root_path(), path).string();
    }

    bool file_exists = Core::File::exists(path);

    if (!file_exists && (m_mode == Mode::Open || m_mode == Mode::OpenFolder)) {
        MessageBox::show(this, DeprecatedString::formatted("No such file or directory: {}", m_filename_textbox->text()), "File not found"sv, MessageBox::Type::Error, MessageBox::InputType::OK);
        return;
    }

    if (file_exists && m_mode == Mode::Save) {
        auto result = MessageBox::show(this, "File already exists. Overwrite?"sv, "Existing File"sv, MessageBox::Type::Warning, MessageBox::InputType::OKCancel);
        if (result == MessageBox::ExecResult::Cancel)
            return;
    }

    m_selected_file = path;
    done(ExecResult::OK);
}

void FilePicker::set_path(DeprecatedString const& path)
{
    if (path == "recent:") {
        if (!is<RecentFilesModel>(*m_model))
            MUST(setup_recent_model());
        return;
    }
    if (!is<FileSystemModel>(*m_model))
        setup_filesystem_model(path);
    auto& model = verify_cast<FileSystemModel>(*m_model);

    if (access(path.characters(), R_OK | X_OK) == -1) {
        GUI::MessageBox::show(this, DeprecatedString::formatted("Could not open '{}':\n{}", path, strerror(errno)), "Error"sv, GUI::MessageBox::Type::Error);
        for (auto& location_button : m_common_location_buttons)
            m_common_locations_tray->set_item_checked(location_button.tray_item_index, model.root_path() == location_button.path);
        return;
    }

    auto new_path = LexicalPath(path).string();
    m_location_textbox->set_icon(FileIconProvider::icon_for_path(new_path).bitmap_for_size(16));
    model.set_root_path(new_path);
}

}
