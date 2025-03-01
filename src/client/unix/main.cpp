
#include "TrayIcon.h"
#include "client/Settings.h"
#include "client/ClientState.h"
#include "config/StringTyper.h"
#include "common/output.h"
#include <sstream>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <pwd.h>

#if defined(ENABLE_COCOA)
extern void showMessageBoxCocoa(const char* message, const char* title);
#endif

namespace {
  class ClientStateImpl final : public ClientState, public TrayIcon::Handler {
  public:
    void on_toggle_active() override;
    void on_open_config() override;
    void on_reload_config() override;
    void on_request_next_key_info() override;
    void on_open_help() override;
    void on_open_about() override;
    void on_exit() override;
  };
  
  const auto system_config_path = std::filesystem::path("/etc/");
  const auto update_interval = std::chrono::milliseconds(50);

  Settings g_settings;
  bool g_shutdown;
  bool g_auto_update_config;
  ClientStateImpl g_state;
  TrayIcon g_tray_icon;

  void show_notification(const char* message) {
    auto escaped = std::string(message);
    replace_all<char>(escaped, "\\", "\\\\\\");
    replace_all<char>(escaped, "\"", "\\\"");
    auto ss = std::stringstream();
#if defined(__APPLE__)
    ss << R"(osascript -e 'display notification ")" << escaped << R"(" with title "keymapper"')";
#else
    ss << R"(notify-send -a keymapper keymapper ")" << escaped << R"(")";
#endif
    [[maybe_unused]] auto result = std::system(ss.str().c_str());
  }

  void update_options() {
    const auto settings = apply_config_options(g_settings, g_state.config());
    g_auto_update_config = settings.auto_update_config;
    g_verbose_output = settings.verbose;
    g_show_notification = (!settings.no_notify ? &show_notification : nullptr);
    if (!settings.no_tray_icon)
      g_tray_icon.initialize(&g_state, !auto_update_config);
    else
      g_tray_icon.reset();
  }

  void ClientStateImpl::on_toggle_active() {
    g_state.toggle_active();
  }
  
  bool open(std::string command) {
#if !defined(__APPLE__)
    command = "xdg-open " + command;
#else
    command = "open -e " + command;
#endif
    return (std::system(command.c_str()) == 0);
  }

  void ClientStateImpl::on_open_config() {
    const auto& filename = g_state.config_filename();

    // create if it does not exist
    auto error = std::error_code{ };
    if (!std::filesystem::exists(filename, error)) {
      if (auto file = std::fopen(filename.string().c_str(), "w")) {
        std::fputc('\n', file);
        std::fclose(file);
      }
    }

    open(filename);
  }
  
  void ClientStateImpl::on_reload_config() {
    g_state.update_config(false);
    g_state.send_config();
    update_options();
  }
  
  void ClientStateImpl::on_request_next_key_info() {
    g_state.request_next_key_info();
  }

  void ClientStateImpl::on_open_help() {
    open("https://github.com/houmain/keymapper");
  }
  
  void ClientStateImpl::on_open_about() {
    message("Version %s\n\n%s", about_header, about_footer);
  }
  
  void ClientStateImpl::on_exit() {
    g_shutdown = true;
  }

  void catch_child([[maybe_unused]] int sig_num) {
    auto child_status = 0;
    ::wait(&child_status);
  }

  void main_loop() {
    while (!g_shutdown) {
      if (g_auto_update_config &&
          g_state.update_config(true)) {
        if (!g_state.send_config())
          return;
        update_options();
      }

      if (g_state.update_active_contexts())
        if (!g_state.send_active_contexts())
          return;

      if (!g_state.read_server_messages(update_interval))
        return;

      g_state.accept_control_connection();
      g_state.read_control_messages();
      g_tray_icon.update();
    }
  }

  int connection_loop() {
    for (;;) {
      if (g_shutdown)
        return 0;

      if (!g_state.connect_server())
        return 1;

      if (!g_state.send_config())
        return 1;

      if (!g_state.initialize_contexts())
        return 1;

      g_state.listen_for_control_connections();

      // shows tray icon
      update_options();

      verbose("Entering update loop");
      main_loop();
      verbose("Connection to keymapperd lost");

      g_state.on_server_disconnected();
      g_tray_icon.reset();

      verbose("---------------");
    }
  }

  std::filesystem::path get_home_path() {
    if (auto homedir = ::getenv("HOME"))
      return homedir;
    return ::getpwuid(::getuid())->pw_dir;
  }

  std::filesystem::path get_config_path() {
    if (auto dir = ::getenv("XDG_CONFIG_HOME"))
      return dir;
    return get_home_path() / ".config";
  }

  std::filesystem::path resolve_config_file_path(
      std::filesystem::path filename) {
    auto error = std::error_code{ };
    if (!filename.empty())
      return std::filesystem::absolute(filename, error);

    filename = default_config_filename;
    for (const auto& base : {
        get_config_path(),
        system_config_path
      }) {
      auto path = base / filename;
      if (std::filesystem::exists(path, error))
        return path;

      path = base / "keymapper" / filename;
      if (std::filesystem::exists(path, error))
        return path;
    }
    // create in profile path when opening for editing
    if (!std::filesystem::exists(filename, error))
      return get_config_path() / filename;

    return std::filesystem::absolute(filename, error);
  }
} // namespace

bool execute_terminal_command(const std::string& command) {
  if (fork() == 0) {
    dup2(open("/dev/null", O_RDONLY), STDIN_FILENO);
    if (!g_verbose_output) {
      dup2(open("/dev/null", O_RDWR), STDOUT_FILENO);
      dup2(open("/dev/null", O_RDWR), STDERR_FILENO);
    }
    execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
    exit(1);
  }
  return true;
}

int main(int argc, char* argv[]) {
  if (!interpret_commandline(g_settings, argc, argv)) {
    print_help_message();
    return 1;
  }
  g_verbose_output = g_settings.verbose;
  if (!g_settings.no_notify)
    g_show_notification = &show_notification;

#if defined(ENABLE_COCOA)
  extern void showMessageBoxCocoa(const char* title, const char* message);
  g_show_message_box = &showMessageBoxCocoa;
#else
  g_show_message_box = [](const char* title, const char* message) { 
    show_notification(message);
  };
#endif

  g_settings.config_file_path = 
    resolve_config_file_path(std::move(g_settings.config_file_path));

  if (g_settings.check_config) {
    if (!g_state.load_config(g_settings.config_file_path))
      return 1;
    notify("The configuration is valid");
    return 0;
  }

  ::signal(SIGCHLD, &catch_child);

  verbose("Loading configuration file '%s'", g_settings.config_file_path.c_str());
  if (!g_state.load_config(g_settings.config_file_path)) {
    // exit when there is no configuration and user cannot update it
    if (g_settings.no_tray_icon)
      return 1;
  }
  return connection_loop();
}
