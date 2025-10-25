// TODO: Copyright
#include "WindowWidget.h"
#include "FileUtils.h"
#include "Properties/PropertiesWindow.h"
#include <AK/LexicalPath.h>
#include <LibConfig/Client.h>
#include <LibCore/StandardPaths.h>
#include <LibFileSystem/FileSystem.h>
#include <LibGUI/Clipboard.h>
#include <LibGUI/Desktop.h>
#include <LibGUI/InputBox.h>
#include <LibGUI/MessageBox.h>
#include <LibGUI/PathBreadcrumbbar.h>
#include <LibGUI/Progressbar.h>
#include <LibGUI/Splitter.h>
#include <LibGUI/Statusbar.h>
#include <LibGUI/Toolbar.h>
#include <LibGUI/ToolbarContainer.h>
#include <LibGUI/Window.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace FileManager;

static void do_copy(Vector<ByteString> const& selected_file_paths, FileOperation file_operation);
static void do_paste(ByteString const& target_directory, GUI::Window* window);
static void do_create_link(Vector<ByteString> const& selected_file_paths, GUI::Window* window);
static void do_create_archive(Vector<ByteString> const& selected_file_paths, GUI::Window* window);
static void do_set_wallpaper(ByteString const& file_path, GUI::Window* window);
static void do_unzip_archive(Vector<ByteString> const& selected_file_paths, GUI::Window* window);
static void show_properties(ByteString const& container_dir_path, ByteString const& path, Vector<ByteString> const& selected, GUI::Window* window);

void do_copy(Vector<ByteString> const& selected_file_paths, FileOperation file_operation)
{
    VERIFY(!selected_file_paths.is_empty());

    StringBuilder copy_text;
    if (file_operation == FileOperation::Move) {
        copy_text.append("#cut\n"sv); // This exploits the comment lines in the text/uri-list specification, which might be a bit hackish
    }
    for (auto& path : selected_file_paths) {
        auto url = URL::create_with_file_scheme(path);
        copy_text.appendff("{}\n", url);
    }
    GUI::Clipboard::the().set_data(copy_text.string_view().bytes(), "text/uri-list");
}

void do_paste(ByteString const& target_directory, GUI::Window* window)
{
    auto data_and_type = GUI::Clipboard::the().fetch_data_and_type();
    if (data_and_type.mime_type != "text/uri-list") {
        dbgln("Cannot paste clipboard type {}", data_and_type.mime_type);
        return;
    }
    auto copied_lines = ByteString::copy(data_and_type.data).split('\n');
    if (copied_lines.is_empty()) {
        dbgln("No files to paste");
        return;
    }

    FileOperation file_operation = FileOperation::Copy;
    if (copied_lines[0] == "#cut") { // cut operation encoded as a text/uri-list comment
        file_operation = FileOperation::Move;
        copied_lines.remove(0);
    }

    Vector<ByteString> source_paths;
    for (auto& uri_as_string : copied_lines) {
        if (uri_as_string.is_empty())
            continue;
        URL::URL url = uri_as_string;
        if (!url.is_valid() || url.scheme() != "file") {
            dbgln("Cannot paste URI {}", uri_as_string);
            continue;
        }
        source_paths.append(URL::percent_decode(url.serialize_path()));
    }

    if (!source_paths.is_empty()) {
        if (auto result = run_file_operation(file_operation, source_paths, target_directory, window); result.is_error())
            dbgln("Failed to paste files: {}", result.error());
    }
}

void do_create_link(Vector<ByteString> const& selected_file_paths, GUI::Window* window)
{
    auto path = selected_file_paths.first();
    auto destination = ByteString::formatted("{}/{}", Core::StandardPaths::desktop_directory(), LexicalPath::basename(path));
    if (auto result = FileSystem::link_file(destination, path); result.is_error()) {
        GUI::MessageBox::show(window, ByteString::formatted("Could not create desktop shortcut:\n{}", result.error()), "File Manager"sv,
            GUI::MessageBox::Type::Error);
    }
}

void do_create_archive(Vector<ByteString> const& selected_file_paths, GUI::Window* window)
{
    String archive_name;
    if (GUI::InputBox::show(window, archive_name, "Enter name:"sv, "Create Archive"sv) != GUI::InputBox::ExecResult::OK)
        return;

    auto output_directory_path = LexicalPath(selected_file_paths.first());

    StringBuilder path_builder;
    path_builder.append(output_directory_path.dirname());
    path_builder.append('/');
    if (archive_name.is_empty()) {
        path_builder.append(output_directory_path.parent().basename());
        path_builder.append(".zip"sv);
    } else {
        path_builder.append(archive_name);
        if (!AK::StringUtils::ends_with(archive_name, ".zip"sv, CaseSensitivity::CaseSensitive))
            path_builder.append(".zip"sv);
    }
    auto output_path = path_builder.to_byte_string();

    pid_t zip_pid = fork();
    if (zip_pid < 0) {
        perror("fork");
        VERIFY_NOT_REACHED();
    }

    if (!zip_pid) {
        Vector<ByteString> relative_paths;
        Vector<char const*> arg_list;
        arg_list.append("/bin/zip");
        arg_list.append("-r");
        arg_list.append("-f");
        arg_list.append(output_path.characters());
        for (auto const& path : selected_file_paths) {
            relative_paths.append(LexicalPath::relative_path(path, output_directory_path.dirname()));
            arg_list.append(relative_paths.last().characters());
        }
        arg_list.append(nullptr);
        int rc = execvp("/bin/zip", const_cast<char* const*>(arg_list.data()));
        if (rc < 0) {
            perror("execvp");
            _exit(1);
        }
    } else {
        int status;
        int rc = waitpid(zip_pid, &status, 0);
        if (rc < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            GUI::MessageBox::show(window, "Could not create archive"sv, "Archive Error"sv, GUI::MessageBox::Type::Error);
    }
}

void do_set_wallpaper(ByteString const& file_path, GUI::Window* window)
{
    auto show_error = [&] {
        GUI::MessageBox::show(window, ByteString::formatted("Failed to set {} as wallpaper.", file_path), "Failed to set wallpaper"sv, GUI::MessageBox::Type::Error);
    };

    constexpr auto scale_factor = 1;
    auto bitmap_or_error = Gfx::Bitmap::load_from_file(file_path, scale_factor, GUI::Desktop::the().rect().size());
    if (bitmap_or_error.is_error()) {
        show_error();
        return;
    }

    if (!GUI::Desktop::the().set_wallpaper(bitmap_or_error.release_value(), file_path))
        show_error();
}

void do_unzip_archive(Vector<ByteString> const& selected_file_paths, GUI::Window* window)
{
    ByteString archive_file_path = selected_file_paths.first();
    ByteString output_directory_path = archive_file_path.substring(0, archive_file_path.length() - 4);

    pid_t unzip_pid = fork();
    if (unzip_pid < 0) {
        perror("fork");
        VERIFY_NOT_REACHED();
    }

    if (!unzip_pid) {
        int rc = execlp("/bin/unzip", "/bin/unzip", "-d", output_directory_path.characters(), archive_file_path.characters(), nullptr);
        if (rc < 0) {
            perror("execlp");
            _exit(1);
        }
    } else {
        // FIXME: this could probably be tied in with the new file operation progress tracking
        int status;
        int rc = waitpid(unzip_pid, &status, 0);
        if (rc < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            GUI::MessageBox::show(window, "Could not extract archive"sv, "Extract Archive Error"sv, GUI::MessageBox::Type::Error);
    }
}

void show_properties(ByteString const& container_dir_path, ByteString const& path, Vector<ByteString> const& selected, GUI::Window* window)
{
    ErrorOr<RefPtr<PropertiesWindow>> properties_or_error = nullptr;
    if (selected.is_empty()) {
        properties_or_error = window->try_add<PropertiesWindow>(path, true);
    } else {
        properties_or_error = window->try_add<PropertiesWindow>(selected.first(), access(container_dir_path.characters(), W_OK) != 0);
    }

    if (properties_or_error.is_error()) {
        GUI::MessageBox::show(window, "Could not show properties"sv, "Properties Error"sv, GUI::MessageBox::Type::Error);
        return;
    }

    auto properties = properties_or_error.release_value();
    properties->on_close = [properties = properties.ptr()] {
        properties->remove_from_parent();
    };
    properties->center_on_screen();
    properties->show();
}

namespace FileManager {

ErrorOr<void> WindowWidget::initialize()
{
    m_toolbar_container = find_descendant_of_type_named<GUI::ToolbarContainer>("toolbar_container");
    m_main_toolbar = find_descendant_of_type_named<GUI::Toolbar>("main_toolbar");

    m_breadcrumb_toolbar = find_descendant_of_type_named<GUI::Toolbar>("breadcrumb_toolbar");
    m_breadcrumb_toolbar->layout()->set_margins({ 0, 6 });
    m_breadcrumbbar = find_descendant_of_type_named<GUI::PathBreadcrumbbar>("breadcrumbbar");

    m_tree_view = find_descendant_of_type_named<GUI::TreeView>("tree_view");
    m_directories_model = GUI::FileSystemModel::create({}, GUI::FileSystemModel::Mode::DirectoriesOnly);
    m_tree_view->set_model(m_directories_model);
    m_tree_view->set_column_visible(GUI::FileSystemModel::Column::Icon, false);
    m_tree_view->set_column_visible(GUI::FileSystemModel::Column::Size, false);
    m_tree_view->set_column_visible(GUI::FileSystemModel::Column::User, false);
    m_tree_view->set_column_visible(GUI::FileSystemModel::Column::Group, false);
    m_tree_view->set_column_visible(GUI::FileSystemModel::Column::Permissions, false);
    m_tree_view->set_column_visible(GUI::FileSystemModel::Column::ModificationTime, false);
    m_tree_view->set_column_visible(GUI::FileSystemModel::Column::Inode, false);
    m_tree_view->set_column_visible(GUI::FileSystemModel::Column::SymlinkTarget, false);
    
    // Open the root directory. FIXME: This is awkward.
    m_tree_view->toggle_index(m_directories_model->index(0, 0, {}));
    
    auto& splitter = *find_descendant_of_type_named<GUI::HorizontalSplitter>("splitter");
    m_directory_view = splitter.try_add<DirectoryView>(DirectoryView::Mode::Normal).release_value_but_fixme_should_propagate_errors();
    m_directory_view->set_name("directory_view");

    auto& statusbar = *find_descendant_of_type_named<GUI::Statusbar>("statusbar");
    GUI::Application::the()->on_action_enter = [&statusbar](GUI::Action& action) {
        statusbar.set_override_text(action.status_tip());
    };

    GUI::Application::the()->on_action_leave = [&statusbar](GUI::Action&) {
        statusbar.set_override_text({});
    };

    TRY(setup_actions());
    create_toolbar();

    // FIXME: RIZZME: function a'la load_config?
    auto& progressbar = *find_descendant_of_type_named<GUI::Progressbar>("progressbar");
    progressbar.set_format(GUI::Progressbar::Format::ValueSlashMax);
    progressbar.set_frame_style(Gfx::FrameStyle::SunkenPanel);

    m_show_toolbar = Config::read_bool("FileManager"sv, "Layout"sv, "ShowToolbar"sv, true);
    m_layout_toolbar_action->set_checked(m_show_toolbar);
    m_main_toolbar->set_visible(m_show_toolbar);

    m_show_location = Config::read_bool("FileManager"sv, "Layout"sv, "ShowLocationBar"sv, true);
    m_layout_toolbar_action->set_checked(m_show_location);
    m_breadcrumb_toolbar->set_visible(m_show_location);

    m_toolbar_container->set_visible(m_show_location || m_show_toolbar);

    auto show_statusbar = Config::read_bool("FileManager"sv, "Layout"sv, "ShowStatusbar"sv, true);
    m_layout_statusbar_action->set_checked(show_statusbar);
    statusbar.set_visible(show_statusbar);

    auto show_folderpane = Config::read_bool("FileManager"sv, "Layout"sv, "ShowFolderPane"sv, true);
    m_layout_folderpane_action->set_checked(show_folderpane);
    m_tree_view->set_visible(show_folderpane);

    m_breadcrumbbar->on_hide_location_box = [this] {
        if (m_show_location)
            m_breadcrumb_toolbar->set_visible(true);
        if (!(m_show_location || m_show_toolbar))
            m_toolbar_container->set_visible(false);
    };

    m_breadcrumbbar->on_path_change = [this](auto selected_path) {
        if (FileSystem::is_directory(selected_path)) {
            m_directory_view->open(selected_path);
        } else {
            dbgln("Breadcrumb path '{}' doesn't exist", selected_path);
            m_breadcrumbbar->set_current_path(m_directory_view->path());
        }
    };

    m_directory_view->on_path_change = [this](ByteString const& new_path, bool can_read_in_path, bool can_write_in_path) {
        auto icon = GUI::FileIconProvider::icon_for_path(new_path);
        auto* bitmap = icon.bitmap_for_size(16);
        window()->set_icon(bitmap);

        window()->set_title(ByteString::formatted("{} - File Manager", new_path));

        m_breadcrumbbar->set_current_path(new_path);

        if (!m_is_reacting_to_tree_view_selection_change) {
            auto new_index = m_directories_model->index(new_path, GUI::FileSystemModel::Column::Name);
            if (new_index.is_valid()) {
                m_tree_view->expand_all_parents_of(new_index);
                m_tree_view->set_cursor(new_index, GUI::AbstractView::SelectionUpdate::Set);
            }
        }

        m_mkdir_action->set_enabled(can_write_in_path);
        m_directory_view->touch_action().set_enabled(can_write_in_path);
        m_paste_action->set_enabled(can_write_in_path && GUI::Clipboard::the().fetch_mime_type() == "text/uri-list");
        m_go_forward_action->set_enabled(m_directory_view->path_history_position() < m_directory_view->path_history_size() - 1);
        m_go_back_action->set_enabled(m_directory_view->path_history_position() > 0);
        m_open_parent_directory_action->set_enabled(m_breadcrumbbar->has_parent_segment());
        m_open_child_directory_action->set_enabled(m_breadcrumbbar->has_child_segment());
        m_directory_view->view_as_table_action().set_enabled(can_read_in_path);
        m_directory_view->view_as_icons_action().set_enabled(can_read_in_path);
        m_directory_view->view_as_columns_action().set_enabled(can_read_in_path);
    };

    m_directory_view->on_status_message = [&](StringView message) {
        statusbar.set_text(String::from_utf8(message).release_value_but_fixme_should_propagate_errors());
    };

    m_directory_view->on_thumbnail_progress = [&](int done, int total) {
        if (done == total) {
            progressbar.set_visible(false);
            return;
        }
        progressbar.set_range(0, total);
        progressbar.set_value(done);
        progressbar.set_visible(true);
    };

    m_directory_view->on_selection_change = [this](GUI::AbstractView& view) {
        auto& selection = view.selection();
        m_cut_action->set_enabled(!selection.is_empty() && access(m_directory_view->path().characters(), W_OK) == 0);
        m_copy_action->set_enabled(!selection.is_empty());
        m_copy_path_action->set_text(selection.size() > 1 ? "Copy Paths" : "Copy Path");
        m_focus_dependent_delete_action->set_enabled((!m_tree_view->selection().is_empty() && m_tree_view->is_focused())
            || (!m_directory_view->current_view().selection().is_empty() && access(m_directory_view->path().characters(), W_OK) == 0));
    };

    m_tree_view_directory_context_menu = GUI::Menu::construct("Tree View Directory"_string);
    m_tree_view_directory_context_menu->add_action(m_directory_view->open_window_action());
    m_tree_view_directory_context_menu->add_action(*m_tree_view_open_in_new_terminal_action);
    m_tree_view_directory_context_menu->add_separator();
    m_tree_view_directory_context_menu->add_action(*m_mkdir_action);
    m_tree_view_directory_context_menu->add_action(m_directory_view->touch_action());
    m_tree_view_directory_context_menu->add_action(*m_cut_action);
    m_tree_view_directory_context_menu->add_action(*m_copy_action);
    m_tree_view_directory_context_menu->add_action(*m_copy_path_action);
    m_tree_view_directory_context_menu->add_action(*m_paste_action);
    m_tree_view_directory_context_menu->add_action(*m_tree_view_delete_action);
    m_tree_view_directory_context_menu->add_separator();
    m_tree_view_directory_context_menu->add_action(*m_properties_action);
    
    m_directory_view->setup_empty_space_context_menu([this](auto& menu) {
        menu.add_action(m_directory_view->mkdir_action());
        menu.add_action(m_directory_view->touch_action());
        menu.add_action(*m_paste_action);
        menu.add_action(m_directory_view->open_terminal_action());
        menu.add_separator();
        menu.add_action(*m_show_dotfiles_action);
        menu.add_separator();
        menu.add_action(*m_properties_action);
    });
    m_directory_view->prepare_context_menu = [this](GUI::Menu& menu, GUI::FileSystemModel::Node const& node) {
        if (node.is_directory()) {
            auto should_get_enabled = access(node.full_path().characters(), W_OK) == 0 && GUI::Clipboard::the().fetch_mime_type() == "text/uri-list";
            m_folder_specific_paste_action->set_enabled(should_get_enabled);
        }

        menu.add_action(*m_cut_action);
        menu.add_action(*m_copy_action);
        menu.add_action(*m_copy_path_action);
        menu.add_action(node.is_directory() ? *m_folder_specific_paste_action : *m_paste_action);
        menu.add_action(m_directory_view->delete_action());
        menu.add_action(m_directory_view->rename_action());
        menu.add_action(*m_shortcut_action);
        menu.add_action(*m_create_archive_action);

        if (!node.is_directory()) {
            if (Gfx::Bitmap::is_path_a_supported_image_format(node.name)) {
                menu.add_separator();
                menu.add_action(*m_set_wallpaper_action);
            }

            if (node.name.ends_with(".zip"sv, AK::CaseSensitivity::CaseInsensitive)) {
                menu.add_separator();
                menu.add_action(*m_unzip_archive_action);
            }
        }

        menu.add_separator();
        menu.add_action(*m_properties_action);
    };

    m_tree_view->on_selection_change = [this] {
        m_focus_dependent_delete_action->set_enabled((!m_tree_view->selection().is_empty() && m_tree_view->is_focused())
            || !m_directory_view->current_view().selection().is_empty());

        if (m_tree_view->selection().is_empty())
            return;

        if (m_directories_model->m_previously_selected_index.is_valid())
            m_directories_model->update_node_on_selection(m_directories_model->m_previously_selected_index, false);

        auto const& index = m_tree_view->selection().first();
        m_directories_model->update_node_on_selection(index, true);
        m_directories_model->m_previously_selected_index = index;

        auto path = m_directories_model->full_path(index);
        if (m_directory_view->path() == path)
            return;
        TemporaryChange change(m_is_reacting_to_tree_view_selection_change, true);
        m_directory_view->open(path);
        m_cut_action->set_enabled(!m_tree_view->selection().is_empty());
        m_copy_action->set_enabled(!m_tree_view->selection().is_empty());
        m_directory_view->delete_action().set_enabled(!m_tree_view->selection().is_empty());
    };

    m_tree_view->on_focus_change = [this](bool has_focus, [[maybe_unused]] GUI::FocusSource const source) {
        m_focus_dependent_delete_action->set_enabled((!m_tree_view->selection().is_empty() && has_focus)
            || !m_directory_view->current_view().selection().is_empty());
    };

    m_tree_view->on_context_menu_request = [this](GUI::ModelIndex const& index, GUI::ContextMenuEvent const& event) {
        if (index.is_valid()) {
            m_tree_view_directory_context_menu->popup(event.screen_position());
        }
    };

    m_breadcrumbbar->on_paths_drop = [this](auto path, GUI::DropEvent const& event) {
        handle_drop(event, path, window()).release_value_but_fixme_should_propagate_errors();
    };

    m_tree_view->on_drop = [this](GUI::ModelIndex const& index, GUI::DropEvent const& event) {
        auto const& target_node = m_directories_model->node(index);
        bool const has_accepted_drop = handle_drop(event, target_node.full_path(), window()).release_value_but_fixme_should_propagate_errors();
        if (has_accepted_drop)
            const_cast<GUI::DropEvent&>(event).accept();
    };

    GUI::Clipboard::the().on_change = [this](ByteString const& data_type) {
        auto current_location = m_directory_view->path();
        m_paste_action->set_enabled(data_type == "text/uri-list" && access(current_location.characters(), W_OK) == 0);
    };

    m_directory_view->set_view_mode_from_string(Config::read_string("FileManager"sv, "DirectoryView"sv, "ViewMode"sv, "Icon"sv));
    return {};
}

ErrorOr<void> WindowWidget::initialize_menubar(GUI::Window &window)
{
    auto file_menu = window.add_menu("&File"_string);
    file_menu->add_action(*m_new_window_action);
    file_menu->add_action(*m_mkdir_action);
    file_menu->add_action(m_directory_view->touch_action());
    file_menu->add_action(*m_focus_dependent_delete_action);
    file_menu->add_action(m_directory_view->rename_action());
    file_menu->add_separator();
    file_menu->add_action(*m_properties_action);
    file_menu->add_separator();
    file_menu->add_action(GUI::CommonActions::make_quit_action([](auto&) {
        GUI::Application::the()->quit();
    }));

    auto edit_menu = window.add_menu("&Edit"_string);
    edit_menu->add_action(*m_cut_action);
    edit_menu->add_action(*m_copy_action);
    edit_menu->add_action(*m_paste_action);
    edit_menu->add_separator();
    edit_menu->add_action(*m_select_all_action);

    auto show_dotfiles = Config::read_bool("FileManager"sv, "DirectoryView"sv, "ShowDotFiles"sv, false);
    // show_dotfiles |= initial_location.contains("/."sv); // FIXME: KURWA 
    m_show_dotfiles_action->set_checked(show_dotfiles);
    show_dotfiles_in_view(show_dotfiles);

    auto view_menu = window.add_menu("&View"_string);
    auto layout_menu = view_menu->add_submenu("&Layout"_string);
    layout_menu->add_action(*m_layout_toolbar_action);
    layout_menu->add_action(*m_layout_location_action);
    layout_menu->add_action(*m_layout_statusbar_action);
    layout_menu->add_action(*m_layout_folderpane_action);

    view_menu->add_separator();

    view_menu->add_action(m_directory_view->view_as_icons_action());
    view_menu->add_action(m_directory_view->view_as_table_action());
    view_menu->add_action(m_directory_view->view_as_columns_action());
    view_menu->add_separator();
    view_menu->add_action(*m_show_dotfiles_action);

    view_menu->add_separator();
    view_menu->add_action(GUI::CommonActions::make_fullscreen_action([&](auto&) {
        window.set_fullscreen(!window.is_fullscreen());
    }));

    auto go_menu = window.add_menu("&Go"_string);
    go_menu->add_action(*m_go_back_action);
    go_menu->add_action(*m_go_forward_action);
    go_menu->add_action(*m_open_parent_directory_action);
    go_menu->add_action(*m_open_child_directory_action);
    go_menu->add_action(*m_go_home_action);
    go_menu->add_action(*m_go_to_location_action);
    go_menu->add_separator();
    go_menu->add_action(m_directory_view->open_terminal_action());

    auto help_menu = window.add_menu("&Help"_string);
    help_menu->add_action(GUI::CommonActions::make_command_palette_action(&window));
    help_menu->add_action(GUI::CommonActions::make_about_action("File Manager"_string, GUI::Icon::default_icon("app-file-manager"sv), &window));

    return {};
}

ErrorOr<void> WindowWidget::setup_actions()
{
    auto window = nullptr; // TODO: FIXME: nie ładnie tak.
    auto& statusbar = *find_descendant_of_type_named<GUI::Statusbar>("statusbar");

    m_select_all_action = GUI::CommonActions::make_select_all_action([this](auto&) {
        m_directory_view->current_view().select_all();
    });

    m_cut_action = GUI::CommonActions::make_cut_action(
        [this](auto&) {
            auto paths = m_directory_view->selected_file_paths();
            if (paths.is_empty())
                paths = tree_view_selected_file_paths();
            VERIFY(!paths.is_empty());

            do_copy(paths, FileOperation::Move);
        },
        window);
    m_cut_action->set_enabled(false);

    m_copy_action = GUI::CommonActions::make_copy_action(
        [this](auto&) {
            auto paths = m_directory_view->selected_file_paths();
            if (paths.is_empty())
                paths = tree_view_selected_file_paths();
            VERIFY(!paths.is_empty());

            do_copy(paths, FileOperation::Copy);
        },
        window);
    m_copy_action->set_enabled(false);

    m_copy_path_action = GUI::Action::create(
        "Copy Path", [this](GUI::Action const&) {
            Vector<ByteString> selected_paths;
            if (m_directory_view->active_widget()->is_focused()) {
                selected_paths = m_directory_view->selected_file_paths();
            } else if (m_tree_view->is_focused()) {
                selected_paths = tree_view_selected_file_paths();
            }
            VERIFY(!selected_paths.is_empty());

            StringBuilder joined_paths_builder;
            joined_paths_builder.join('\n', selected_paths.span());
            GUI::Clipboard::the().set_plain_text(joined_paths_builder.string_view());
        },
        window);

    m_tree_view_open_in_new_terminal_action
        = GUI::Action::create(
            "Open in &Terminal",
            {},
            TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/app-terminal.png"sv)),
            [&](GUI::Action const& action) {
                Vector<ByteString> paths;
                if (action.activator() == m_tree_view_directory_context_menu)
                    paths = tree_view_selected_file_paths();
                else
                    paths = m_directory_view->selected_file_paths();

                for (auto& path : paths) {
                    if (FileSystem::is_directory(path)) {
                        spawn_terminal(window, path);
                    }
                }
            },
            window);

    m_directory_open_action = GUI::Action::create("Open", TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/open.png"sv)), [this](auto&) {
        m_directory_view->open(m_directory_view->selected_file_paths().first());
    });

    m_shortcut_action
        = GUI::Action::create(
            "Create Desktop &Shortcut",
            {},
            TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/filetype-symlink.png"sv)),
            [this](GUI::Action const&) {
                auto paths = m_directory_view->selected_file_paths();
                if (paths.is_empty()) {
                    return;
                }
                do_create_link(paths, m_directory_view->window());
            },
            window);

    m_create_archive_action
        = GUI::Action::create(
            "Create &Archive",
            TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/filetype-archive.png"sv)),
            [this](GUI::Action const&) {
                auto paths = m_directory_view->selected_file_paths();
                if (paths.is_empty())
                    return;

                do_create_archive(paths, m_directory_view->window());
            },
            window);

    m_unzip_archive_action
        = GUI::Action::create(
            "E&xtract Here",
            [this](GUI::Action const&) {
                auto paths = m_directory_view->selected_file_paths();
                if (paths.is_empty())
                    return;

                do_unzip_archive(paths, m_directory_view->window());
            },
            window);

    m_set_wallpaper_action
        = GUI::Action::create(
            "Set as Desktop &Wallpaper",
            TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/app-display-settings.png"sv)),
            [this](GUI::Action const&) {
                auto paths = m_directory_view->selected_file_paths();
                if (paths.is_empty())
                    return;

                do_set_wallpaper(paths.first(), m_directory_view->window());
            },
            window);

    m_properties_action = GUI::CommonActions::make_properties_action(
        [this](auto& action) {
            ByteString container_dir_path;
            ByteString path;
            Vector<ByteString> selected;
            if (action.activator() != m_tree_view_directory_context_menu || m_directory_view->active_widget()->is_focused()) {
                path = m_directory_view->path();
                container_dir_path = path;
                selected = m_directory_view->selected_file_paths();
            } else {
                path = m_directories_model->full_path(m_tree_view->selection().first());
                container_dir_path = LexicalPath::basename(path);
                selected = tree_view_selected_file_paths();
            }

            show_properties(container_dir_path, path, selected, m_directory_view->window());
        },
        window);

    m_paste_action = GUI::CommonActions::make_paste_action(
        [this](auto&) {
            ByteString target_directory = m_directory_view->selected_file_paths().is_empty() ? m_directory_view->path() : m_directory_view->selected_file_paths()[0];
            do_paste(target_directory, m_directory_view->window());
        },
        window);

    m_folder_specific_paste_action = GUI::CommonActions::make_paste_action(
        [this](auto&) {
            ByteString target_directory = m_directory_view->selected_file_paths().is_empty() ? m_directory_view->path() : m_directory_view->selected_file_paths()[0];
            do_paste(target_directory, m_directory_view->window());
        },
        window);

    m_go_back_action = GUI::CommonActions::make_go_back_action(
        [this](auto&) {
            m_directory_view->open_previous_directory();
        },
        window);

    m_go_forward_action = GUI::CommonActions::make_go_forward_action(
        [this](auto&) {
            m_directory_view->open_next_directory();
        },
        window);

    m_go_home_action = GUI::CommonActions::make_go_home_action(
        [this](auto&) {
            m_directory_view->open(Core::StandardPaths::home_directory());
        },
        window);

    m_tree_view_delete_action = GUI::CommonActions::make_delete_action(
        [&](auto&) {
            delete_paths(tree_view_selected_file_paths(), true, window);
        },
        m_tree_view);

    // This is a little awkward. The menu action does something different depending on which view has focus.
    // It would be nice to find a good abstraction for this instead of creating a branching action like this.
    m_focus_dependent_delete_action = GUI::CommonActions::make_delete_action([this](auto&) {
        if (m_tree_view->is_focused())
            m_tree_view_delete_action->activate();
        else
            m_directory_view->delete_action().activate();
    });
    m_focus_dependent_delete_action->set_enabled(false);

    m_new_window_action = GUI::Action::create("&New Window", { Mod_Ctrl, Key_N }, TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/new-window.png"sv)), [this](GUI::Action const&) {
        Desktop::Launcher::open(URL::create_with_file_scheme(m_directory_view->path()));
    });

    m_mkdir_action = GUI::Action::create("&New Directory...", { Mod_Ctrl | Mod_Shift, Key_N }, TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/mkdir.png"sv)), [this](GUI::Action const&) {
        m_directory_view->mkdir_action().activate();
    });

    m_go_to_location_action = GUI::Action::create("Go to &Location...", { Mod_Ctrl, Key_L }, Key_F6, TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/go-to.png"sv)), [this](auto&) {
        m_toolbar_container->set_visible(true);
        m_breadcrumb_toolbar->set_visible(true);
        m_breadcrumbbar->show_location_text_box();
    });

    m_open_parent_directory_action = GUI::Action::create("Open &Parent Directory", { Mod_Alt, Key_Up }, TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/open-parent-directory.png"sv)), [this](GUI::Action const&) {
        m_directory_view->open_parent_directory();
    });

    m_open_child_directory_action = GUI::Action::create("Open &Child Directory", { Mod_Alt, Key_Down }, [this](GUI::Action const&) {
        m_breadcrumbbar->select_child_segment();
    });

    m_layout_toolbar_action = GUI::Action::create_checkable("&Toolbar", [this](auto& action) {
        if (action.is_checked()) {
            m_main_toolbar->set_visible(true);
            m_toolbar_container->set_visible(true);
        } else {
            m_main_toolbar->set_visible(false);
            if (!m_breadcrumb_toolbar->is_visible())
                m_toolbar_container->set_visible(false);
        }
        m_show_toolbar = action.is_checked();
        Config::write_bool("FileManager"sv, "Layout"sv, "ShowToolbar"sv, action.is_checked());
    });
    m_layout_location_action = GUI::Action::create_checkable("&Location Bar", [this](auto& action) {
        if (action.is_checked()) {
            m_breadcrumb_toolbar->set_visible(true);
            m_toolbar_container->set_visible(true);
        } else {
            m_breadcrumb_toolbar->set_visible(false);
            if (!m_main_toolbar->is_visible())
                m_toolbar_container->set_visible(false);
        }
        m_show_location = action.is_checked();
        Config::write_bool("FileManager"sv, "Layout"sv, "ShowLocationBar"sv, action.is_checked());
    });

    m_layout_statusbar_action = GUI::Action::create_checkable("&Status Bar", [&](auto& action) {
        action.is_checked() ? statusbar.set_visible(true) : statusbar.set_visible(false);
        Config::write_bool("FileManager"sv, "Layout"sv, "ShowStatusbar"sv, action.is_checked());
    });

    m_layout_folderpane_action = GUI::Action::create_checkable("&Folder Pane", { Mod_Ctrl, Key_P }, [this](auto& action) {
        action.is_checked() ? m_tree_view->set_visible(true) : m_tree_view->set_visible(false);
        Config::write_bool("FileManager"sv, "Layout"sv, "ShowFolderPane"sv, action.is_checked());
    });

    m_show_dotfiles_action = GUI::Action::create_checkable("&Show Dotfiles", { Mod_Ctrl, Key_H }, [&](auto& action) {
        show_dotfiles_in_view(action.is_checked());
        refresh_tree_view();
        Config::write_bool("FileManager"sv, "DirectoryView"sv, "ShowDotFiles"sv, action.is_checked());
    });

    m_view_type_action_group = make<GUI::ActionGroup>();
    m_view_type_action_group->set_exclusive(true);
    m_view_type_action_group->add_action(m_directory_view->view_as_icons_action());
    m_view_type_action_group->add_action(m_directory_view->view_as_table_action());
    m_view_type_action_group->add_action(m_directory_view->view_as_columns_action());

    return {};
}

void WindowWidget::create_toolbar()
{
    m_main_toolbar->add_action(*m_go_back_action);
    m_main_toolbar->add_action(*m_go_forward_action);
    m_main_toolbar->add_action(*m_open_parent_directory_action);
    m_main_toolbar->add_action(*m_go_home_action);

    m_main_toolbar->add_separator();
    m_main_toolbar->add_action(m_directory_view->open_terminal_action());

    m_main_toolbar->add_separator();
    m_main_toolbar->add_action(*m_mkdir_action);
    m_main_toolbar->add_action(m_directory_view->touch_action());
    m_main_toolbar->add_separator();

    m_main_toolbar->add_action(*m_focus_dependent_delete_action);
    m_main_toolbar->add_action(m_directory_view->rename_action());

    m_main_toolbar->add_separator();
    m_main_toolbar->add_action(*m_cut_action);
    m_main_toolbar->add_action(*m_copy_action);
    m_main_toolbar->add_action(*m_paste_action);

    m_main_toolbar->add_separator();
    m_main_toolbar->add_action(m_directory_view->view_as_icons_action());
    m_main_toolbar->add_action(m_directory_view->view_as_table_action());
    m_main_toolbar->add_action(m_directory_view->view_as_columns_action());
}

void WindowWidget::open(ByteString const& initial_location, ByteString const& entry_focused_on_init)
{
    m_directory_view->open(initial_location);
    m_directory_view->set_focus(true);
    m_paste_action->set_enabled(GUI::Clipboard::the().fetch_mime_type() == "text/uri-list" && access(initial_location.characters(), W_OK) == 0);

    if (!entry_focused_on_init.is_empty()) {
        auto matches = m_directory_view->current_view().model()->matches(entry_focused_on_init, GUI::Model::MatchesFlag::MatchFull | GUI::Model::MatchesFlag::FirstMatchOnly);
        if (!matches.is_empty())
            m_directory_view->current_view().set_cursor(matches.first(), GUI::AbstractView::SelectionUpdate::Set);
    }
}

void WindowWidget::refresh_tree_view()
{
    m_directories_model->invalidate();

    auto current_path = m_directory_view->path();

    struct stat st;
    // If the directory no longer exists, we find a parent that does.
    while (stat(current_path.characters(), &st) != 0) {
        m_directory_view->open_parent_directory();
        current_path = m_directory_view->path();
        if (current_path == m_directories_model->root_path()) {
            break;
        }
    }

    // Reselect the existing folder in the tree.
    auto new_index = m_directories_model->index(current_path, GUI::FileSystemModel::Column::Name);
    if (new_index.is_valid()) {
        m_tree_view->expand_all_parents_of(new_index);
        m_tree_view->set_cursor(new_index, GUI::AbstractView::SelectionUpdate::Set, true);
    }

    m_directory_view->refresh();
}

} // namespace FileManager
