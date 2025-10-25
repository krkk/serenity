/*
 * Copyright (c) 2018-2021, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021-2023, Sam Atkins <atkinssj@serenityos.org>
 * Copyright (c) 2021, Mustafa Quraish <mustafa@cs.toronto.edu>
 * Copyright (c) 2022-2023, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "DesktopWidget.h"
#include "DirectoryView.h"
#include "FileUtils.h"
#include "Properties/PropertiesWindow.h"
#include "WindowWidget.h"
#include <AK/LexicalPath.h>
#include <AK/StringBuilder.h>
#include <AK/Try.h>
#include <LibConfig/Client.h>
#include <LibConfig/Listener.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/Process.h>
#include <LibCore/StandardPaths.h>
#include <LibCore/System.h>
#include <LibDesktop/Launcher.h>
#include <LibFileSystem/FileSystem.h>
#include <LibFileSystem/TempFile.h>
#include <LibGUI/Action.h>
#include <LibGUI/ActionGroup.h>
#include <LibGUI/Application.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Clipboard.h>
#include <LibGUI/Desktop.h>
#include <LibGUI/FileIconProvider.h>
#include <LibGUI/FileSystemModel.h>
#include <LibGUI/InputBox.h>
#include <LibGUI/Menu.h>
#include <LibGUI/Menubar.h>
#include <LibGUI/MessageBox.h>
#include <LibGUI/Painter.h>
#include <LibGUI/PathBreadcrumbbar.h>
#include <LibGUI/Progressbar.h>
#include <LibGUI/Splitter.h>
#include <LibGUI/Statusbar.h>
#include <LibGUI/Toolbar.h>
#include <LibGUI/ToolbarContainer.h>
#include <LibGUI/TreeView.h>
#include <LibGUI/Widget.h>
#include <LibGUI/Window.h>
#include <LibGfx/Palette.h>
#include <LibMain/Main.h>
#include <LibURL/URL.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace FileManager;

static ErrorOr<int> run_in_desktop_mode();
static ErrorOr<int> run_in_windowed_mode(ByteString const& initial_location, ByteString const& entry_focused_on_init);
static void do_copy(Vector<ByteString> const& selected_file_paths, FileOperation file_operation);
static void do_paste(ByteString const& target_directory, GUI::Window* window);
static void do_create_archive(Vector<ByteString> const& selected_file_paths, GUI::Window* window);
static void do_set_wallpaper(ByteString const& file_path, GUI::Window* window);
static void do_unzip_archive(Vector<ByteString> const& selected_file_paths, GUI::Window* window);
static void show_properties(ByteString const& container_dir_path, ByteString const& path, Vector<ByteString> const& selected, GUI::Window* window);

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    TRY(Core::System::pledge("stdio thread recvfd sendfd unix cpath rpath wpath fattr proc exec sigaction"));

    struct sigaction act = {};
    act.sa_flags = SA_NOCLDWAIT;
    act.sa_handler = SIG_IGN;
    TRY(Core::System::sigaction(SIGCHLD, &act, nullptr));

    Core::ArgsParser args_parser;
    bool is_desktop_mode { false };
    bool is_selection_mode { false };
    bool ignore_path_resolution { false };
    ByteString initial_location;
    args_parser.add_option(is_desktop_mode, "Run in desktop mode", "desktop", 'd');
    args_parser.add_option(is_selection_mode, "Show entry in parent folder", "select", 's');
    args_parser.add_option(ignore_path_resolution, "Use raw path, do not resolve real path", "raw", 'r');
    args_parser.add_positional_argument(initial_location, "Path to open", "path", Core::ArgsParser::Required::No);
    args_parser.parse(arguments);

    auto app = TRY(GUI::Application::create(arguments));

    TRY(Core::System::pledge("stdio thread recvfd sendfd cpath rpath wpath fattr proc exec unix"));

    Config::pledge_domains({ "FileManager", "WindowManager", "Maps" });
    Config::monitor_domain("FileManager");
    Config::monitor_domain("WindowManager");

    if (is_desktop_mode)
        return run_in_desktop_mode();

    // our initial location is defined as, in order of precedence:
    // 1. the command-line path argument (e.g. FileManager /bin)
    // 2. the current directory
    // 3. the user's home directory
    // 4. the root directory

    LexicalPath path(initial_location);
    if (!initial_location.is_empty()) {
        if (auto error_or_path = FileSystem::real_path(initial_location); !ignore_path_resolution && !error_or_path.is_error())
            initial_location = error_or_path.release_value();

        if (!FileSystem::is_directory(initial_location)) {
            // We want to extract zips to a temporary directory when FileManager is launched with a .zip file as its first argument
            if (path.has_extension(".zip"sv)) {
                auto temp_directory = FileSystem::TempFile::create_temp_directory();
                if (temp_directory.is_error()) {
                    dbgln("Failed to create temporary directory during zip extraction: {}", temp_directory.error());

                    GUI::MessageBox::show_error(app->active_window(), "Failed to create temporary directory!"sv);
                    return -1;
                }

                auto temp_directory_path = temp_directory.value()->path();
                auto result = Core::Process::spawn("/bin/unzip"sv, Array { "-d"sv, temp_directory_path, initial_location });

                if (result.is_error()) {
                    dbgln("Failed to extract {} to {}: {}", initial_location, temp_directory_path, result.error());

                    auto message = TRY(String::formatted("Failed to extract {} to {}", initial_location, temp_directory_path));
                    GUI::MessageBox::show_error(app->active_window(), message);

                    return -1;
                }

                return run_in_windowed_mode(temp_directory_path.to_byte_string(), path.basename());
            }

            is_selection_mode = true;
        }
    }

    if (auto error_or_cwd = FileSystem::current_working_directory(); initial_location.is_empty() && !error_or_cwd.is_error())
        initial_location = error_or_cwd.release_value();

    if (initial_location.is_empty())
        initial_location = Core::StandardPaths::home_directory();

    if (initial_location.is_empty())
        initial_location = "/";

    ByteString focused_entry;
    if (is_selection_mode) {
        initial_location = path.dirname();
        focused_entry = path.basename();
    }

    return run_in_windowed_mode(initial_location, focused_entry);
}

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

ErrorOr<int> run_in_desktop_mode()
{
    (void)Core::Process::set_name("FileManager (Desktop)"sv, Core::Process::SetThreadName::Yes);

    auto window = GUI::Window::construct();
    window->set_title("Desktop Manager");
    window->set_window_type(GUI::WindowType::Desktop);
    window->set_has_alpha_channel(true);

    auto desktop_icon = TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/desktop.png"sv));
    window->set_icon(desktop_icon);

    auto desktop_widget = window->set_main_widget<FileManager::DesktopWidget>();
    desktop_widget->set_layout<GUI::VerticalBoxLayout>();

    auto directory_view = TRY(desktop_widget->try_add<DirectoryView>(DirectoryView::Mode::Desktop));
    directory_view->set_name("directory_view");

    auto cut_action = GUI::CommonActions::make_cut_action(
        [&](auto&) {
            auto paths = directory_view->selected_file_paths();
            VERIFY(!paths.is_empty());

            do_copy(paths, FileOperation::Move);
        },
        window);
    cut_action->set_enabled(false);

    auto copy_action = GUI::CommonActions::make_copy_action(
        [&](auto&) {
            auto paths = directory_view->selected_file_paths();
            VERIFY(!paths.is_empty());

            do_copy(paths, FileOperation::Copy);
        },
        window);
    copy_action->set_enabled(false);

    auto create_archive_action
        = GUI::Action::create(
            "Create &Archive",
            TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/filetype-archive.png"sv)),
            [&](GUI::Action const&) {
                auto paths = directory_view->selected_file_paths();
                if (paths.is_empty())
                    return;

                do_create_archive(paths, directory_view->window());
            },
            window);

    auto unzip_archive_action
        = GUI::Action::create(
            "E&xtract Here",
            [&](GUI::Action const&) {
                auto paths = directory_view->selected_file_paths();
                if (paths.is_empty())
                    return;

                do_unzip_archive(paths, directory_view->window());
            },
            window);

    auto set_wallpaper_action
        = GUI::Action::create(
            "Set as Desktop &Wallpaper",
            TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/app-display-settings.png"sv)),
            [&](GUI::Action const&) {
                auto paths = directory_view->selected_file_paths();
                if (paths.is_empty())
                    return;

                do_set_wallpaper(paths.first(), directory_view->window());
            },
            window);

    directory_view->on_selection_change = [&](GUI::AbstractView const& view) {
        cut_action->set_enabled(!view.selection().is_empty());
        copy_action->set_enabled(!view.selection().is_empty());
    };

    auto properties_action = GUI::CommonActions::make_properties_action(
        [&](auto&) {
            ByteString path = directory_view->path();
            Vector<ByteString> selected = directory_view->selected_file_paths();

            show_properties(path, path, selected, directory_view->window());
        },
        window);

    auto paste_action = GUI::CommonActions::make_paste_action(
        [&](GUI::Action const&) {
            do_paste(directory_view->path(), directory_view->window());
        },
        window);
    paste_action->set_enabled(GUI::Clipboard::the().fetch_mime_type() == "text/uri-list" && access(directory_view->path().characters(), W_OK) == 0);

    GUI::Clipboard::the().on_change = [&](ByteString const& data_type) {
        paste_action->set_enabled(data_type == "text/uri-list" && access(directory_view->path().characters(), W_OK) == 0);
    };

    auto display_properties_action = GUI::Action::create("&Display Settings", {}, TRY(Gfx::Bitmap::load_from_file("/res/icons/16x16/app-display-settings.png"sv)), [&](GUI::Action const&) {
        Desktop::Launcher::open(URL::create_with_file_scheme("/bin/DisplaySettings"));
    });

    directory_view->setup_empty_space_context_menu([&](auto& menu) {
        menu.add_action(directory_view->mkdir_action());
        menu.add_action(directory_view->touch_action());
        menu.add_action(paste_action);
        menu.add_separator();
        menu.add_action(directory_view->open_window_action());
        menu.add_action(directory_view->open_terminal_action());
        menu.add_separator();
        menu.add_action(display_properties_action);
    });
    directory_view->prepare_context_menu = [&](GUI::Menu& menu, GUI::FileSystemModel::Node const& node) {
        menu.add_action(cut_action);
        menu.add_action(copy_action);
        menu.add_action(paste_action);
        menu.add_action(directory_view->delete_action());
        menu.add_action(directory_view->rename_action());

        if (!node.is_directory()) {
            menu.add_action(create_archive_action);

            if (Gfx::Bitmap::is_path_a_supported_image_format(node.name)) {
                menu.add_separator();
                menu.add_action(set_wallpaper_action);
            }

            if (node.name.ends_with(".zip"sv, AK::CaseSensitivity::CaseInsensitive)) {
                menu.add_separator();
                menu.add_action(unzip_archive_action);
            }
        }

        menu.add_separator();
        menu.add_action(properties_action);
    };

    struct BackgroundWallpaperListener : Config::Listener {
        virtual void config_string_did_change(StringView domain, StringView group, StringView key, StringView value) override
        {
            if (domain == "WindowManager" && group == "Background" && key == "Wallpaper")
                GUI::Desktop::the().apply_wallpaper(nullptr, value);
        }
    } wallpaper_listener;

    // This sets the wallpaper at startup, even if there is no wallpaper, the
    // desktop should still show the background color. It's fine to pass a
    // nullptr to Desktop::set_wallpaper.
    GUI::Desktop::the().load_current_wallpaper();

    // Update wallpaper if desktop resolution changes.
    GUI::Desktop::the().on_receive_screen_rects([&](auto&) {
        GUI::Desktop::the().load_current_wallpaper();
    });

    window->show();
    return GUI::Application::the()->exec();
}

ErrorOr<int> run_in_windowed_mode(ByteString const& initial_location, ByteString const& entry_focused_on_init)
{
    auto window = GUI::Window::construct();
    window->set_title("File Manager");

    auto widget = TRY(WindowWidget::try_create());
    window->set_main_widget(widget);
    TRY(widget->initialize_menubar(*window));
    widget->open(initial_location, entry_focused_on_init);

    window->restore_size_and_position("FileManager"sv, "Window"sv, { { 640, 480 } });
    window->save_size_and_position_on_close("FileManager"sv, "Window"sv);

    window->show();

    return GUI::Application::the()->exec();
}
