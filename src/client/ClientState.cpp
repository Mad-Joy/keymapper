
#include "ClientState.h"
#include "config/get_key_name.h"
#include "config/ParseKeySequence.h"
#include "common/output.h"
#include <sstream>
#include <utility>

extern bool execute_terminal_command(const std::string& command);

namespace {
  KeySequence replace_logical_keys(KeySequence sequence) {
    for (auto& event : sequence) {
      if (event.key == Key::Shift)
        event.key = Key::ShiftLeft;
      if (event.key == Key::Control)
        event.key = Key::ControlLeft;
      if (event.key == Key::Meta)
        event.key = Key::MetaLeft;
    }
    return sequence;
  }

  KeySequence ensure_all_keys_up(KeySequence sequence) {
    static std::vector<Key> s_keys_down;
    s_keys_down.clear();

    for (const auto& event: sequence) {
      auto it = std::find(begin(s_keys_down), end(s_keys_down), event.key);
      if (event.state == KeyState::Down && it == end(s_keys_down)) {
        s_keys_down.push_back(event.key);
      }
      else if (event.state == KeyState::Up && it != end(s_keys_down)) {
        s_keys_down.erase(it);
      }
    }
    for (auto key : s_keys_down)
      sequence.emplace_back(key, KeyState::Up);
    return sequence;
  }
} // namespace

void ClientState::on_execute_action_message(int triggered_action) {
  const auto& actions = m_config_file.config().actions;
  if (triggered_action >= 0 &&
      triggered_action < static_cast<int>(actions.size())) {
    const auto& action = actions[triggered_action];
    const auto& command = action.terminal_command;
    const auto succeeded = execute_terminal_command(command);
    verbose("Executing terminal command '%s'%s", 
      command.c_str(), succeeded ? "" : " failed");
  }
}

void ClientState::on_virtual_key_state_message(Key key, KeyState state) {
  m_control.on_virtual_key_state_changed(key, state);
}

void ClientState::on_set_virtual_key_state_message(Key key, KeyState state) {
  m_server.send_set_virtual_key_state(key, state);
}

bool ClientState::on_set_config_file_message(std::string filename) {
  if (load_config(filename))
    return send_config();
  return false;
}

const std::filesystem::path& ClientState::config_filename() const {
  return m_config_file.filename();
}

bool ClientState::is_focused_window_inaccessible() const {
  return m_focused_window.is_inaccessible();
}

std::optional<Socket> ClientState::connect_server() {
  verbose("Connecting to keymapperd");
  if (m_server.connect())
    return m_server.socket();
  error("Connecting to keymapperd failed");
  return { };
}

bool ClientState::read_server_messages(std::optional<Duration> timeout) {
  return m_server.read_messages(*this, timeout);
}

void ClientState::on_server_disconnected() {
  m_control.reset();
  m_server.disconnect();
  m_focused_window.shutdown();
}

bool ClientState::load_config(std::filesystem::path filename) {
  if (m_config_file.filename() == filename)
    return m_config_file.update(false);

  auto& recent = m_recent_config_files;
  const auto it = std::find_if(recent.begin(), recent.end(), 
    [&](const ConfigFile& cf) { return cf.filename() == filename; });
  if (it == recent.end()) {
    // try to load new file and on success move current to recent list
    auto config_file = ConfigFile();
    if (!config_file.load(std::move(filename))) {
      // store initial config filename to allow reload
      if (!m_config_file)
        m_config_file = std::move(config_file);
      return false;
    }
    if (m_config_file)
      recent.emplace_back(std::move(m_config_file));
    m_config_file = std::move(config_file);
    return true;
  }
  else {
    // swap with previously loaded file
    std::swap(*it, m_config_file);
    return m_config_file.update(false);
  }
}

bool ClientState::update_config(bool check_modified) {
  if (!m_config_file.update(check_modified))
    return false;
  notify("Configuration updated");
  return true;
}

const Config& ClientState::config() const {
  return m_config_file.config();
}

bool ClientState::send_config() {
  verbose("Sending configuration");
  if (!m_server.send_config(m_config_file.config())) {
    error("Sending configuration failed");
    return false;
  }

  m_control.set_virtual_key_aliases(
    m_config_file.config().virtual_key_aliases);
  
  clear_active_contexts();
  if (m_active) {
    update_active_contexts();
    send_active_contexts();
  }
  return true;
}

void ClientState::toggle_active() {
  m_active = !m_active;
  if (m_active)
    update_active_contexts();
  else
    clear_active_contexts();
  send_active_contexts();
}

bool ClientState::send_validate_state() {
  return m_server.send_validate_state();
}

bool ClientState::initialize_contexts() {
  verbose("Initializing focused window detection:");
  return m_focused_window.initialize();
}

void ClientState::clear_active_contexts() {
  m_active_contexts.clear();
}

bool ClientState::update_active_contexts() {
  if (!m_active)
    return false;

  if (m_focused_window.update()) {
    verbose("Detected focused window changed:");
    verbose("  class = '%s'", m_focused_window.window_class().c_str());
    verbose("  title = '%s'", m_focused_window.window_title().c_str());
    verbose("  path = '%s'", m_focused_window.window_path().c_str());
  }
  else {
    if (!m_active_contexts.empty())
      return false;
  }

  m_new_active_contexts.clear();
  const auto& contexts = m_config_file.config().contexts;
  for (auto i = 0; i < static_cast<int>(contexts.size()); ++i)
    if (contexts[i].matches(
        m_focused_window.window_class(),
        m_focused_window.window_title(),
        m_focused_window.window_path()))
      m_new_active_contexts.push_back(i);

  if (m_new_active_contexts != m_active_contexts) {
    verbose("Active contexts updated");
    m_active_contexts.swap(m_new_active_contexts);
    return true;
  }
  return false;
}

bool ClientState::send_active_contexts() {
  verbose("Sending active contexts (%u)", m_active_contexts.size());
  return m_server.send_active_contexts(m_active_contexts);
}

std::optional<Socket> ClientState::listen_for_control_connections() {
  return m_control.listen();
}

std::optional<Socket> ClientState::accept_control_connection() {
  return m_control.accept();
}

void ClientState::read_control_messages() {
  m_control.read_messages(*this);
}

void ClientState::on_next_key_info_message(Key key, DeviceDesc device) {
  extern const char* current_system;
  auto ss = std::stringstream();
  if (auto key_name = get_key_name(key))
    ss << "key_name = '" << key_name << "'\n";
  ss << "scan_code = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << *key << "\n";
  ss << "system = '" << current_system << "'\n";
  if (!m_focused_window.window_title().empty())
    ss << "title = '" << m_focused_window.window_title() << "'\n";
  if (!m_focused_window.window_class().empty())
    ss << "class = '" << m_focused_window.window_class() << "'\n";
  if (!m_focused_window.window_path().empty())
    ss << "path = '" << m_focused_window.window_path() << "'\n";
  if (!device.name.empty())
    ss << "device = '" << device.name << "'\n";
  if (!device.id.empty())
    ss << "device-id = '" << device.id << "'\n";
  auto next_key_info = ss.str();
  next_key_info.pop_back();
  if (!m_control.reply_next_key_info(next_key_info))
    show_next_key_info(next_key_info);
}

void ClientState::show_next_key_info(const std::string& next_key_info) {
  message("%s", next_key_info.c_str());
}

void ClientState::on_next_key_info_requested_message() {
  request_next_key_info();
}

void ClientState::request_next_key_info() {
  m_server.send_request_next_key_info();
}

bool ClientState::on_inject_input_message(const std::string& string) try {
  static auto s_parse_sequence = ParseKeySequence();
  const auto sequence = ensure_all_keys_up(
    replace_logical_keys(s_parse_sequence(string, false)));
  m_server.send_inject_input(sequence);
  return true;
}
catch (...) {
  return false;
}


bool ClientState::on_inject_output_message(const std::string& string) try {
  static auto s_parse_sequence = ParseKeySequence();
  const auto sequence = ensure_all_keys_up(
    replace_logical_keys(s_parse_sequence(string, false)));
  m_server.send_inject_output(sequence);
  return true;
}
catch (...) {
  return false;
}
