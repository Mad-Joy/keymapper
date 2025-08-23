#pragma once

#include "runtime/KeyEvent.h"
#include "common/MessageType.h"
#include "common/Host.h"
#include <memory>
#include <vector>
#include <optional>
#include <array>
#include <map>

class ControlPort {
public:
  ControlPort();
  void reset();
  std::optional<Socket> listen();
  std::optional<Socket> accept();
  void set_virtual_key_aliases(std::vector<std::pair<std::string, Key>> aliases);
  void on_virtual_key_state_changed(Key key, KeyState state);
  bool reply_next_key_info(const std::string& key_info);

  struct MessageHandler {
    virtual void on_set_virtual_key_state_message(Key key, KeyState state) = 0;
    virtual bool on_set_config_file_message(std::string filename) = 0;
    virtual void on_next_key_info_requested_message() = 0;
    virtual bool on_inject_input_message(const std::string& string) = 0;
    virtual bool on_inject_output_message(const std::string& string) = 0;
    virtual bool on_notify_message(const std::string& string) = 0;
  };
  void read_messages(MessageHandler& handler);

private:
  struct Control {
    Connection connection;
    std::string instance_id;
    Key requested_virtual_key_toggle_notification{ };
    bool requested_next_key_info{ };
  };

  Control* get_control(const Connection& connection);
  Key get_virtual_key(const std::string_view name) const;
  KeyState get_virtual_key_state(Key key) const;
  bool read_messages(Connection& connection, MessageHandler& handler);
  bool send_virtual_key_state(Connection& connection, Key key);
  bool send_virtual_key_state(Connection& connection, Key key, KeyState state);
  void send_virtual_key_toggle_notification(Key key);
  void on_virtual_key_toggle_notification_requested(
    Connection& connection, Key key);
  void on_next_key_info_requested(Connection& connection);
  void on_set_instance_id(Connection& connection, std::string id);
  void disconnect_by_instance_id(const std::string& id);

  Host m_host;
  std::array<bool, *Key::last_virtual - *Key::first_virtual> m_virtual_keys_down{ };
  std::vector<std::pair<std::string, Key>> m_virtual_key_aliases;
  std::map<Socket, Control> m_controls;
};
