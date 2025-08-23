
#include "ClientPort.h"

ClientPort::ClientPort() 
  : m_host("keymapperctl") {
}

bool ClientPort::connect(std::optional<Duration> timeout) {
  m_connection = m_host.connect(timeout);
  return static_cast<bool>(m_connection);
}

bool ClientPort::send_get_virtual_key_state(std::string_view name) {
  return m_connection.send_message([&](Serializer& s) {
    s.write(MessageType::get_virtual_key_state);
    s.write(name);
  });
}

bool ClientPort::send_set_virtual_key_state(std::string_view name, KeyState state) {
  return m_connection.send_message([&](Serializer& s) {
    s.write(MessageType::set_virtual_key_state);
    s.write(name);
    s.write(state);
  });
}

bool ClientPort::send_request_virtual_key_toggle_notification(std::string_view name) {
  return m_connection.send_message([&](Serializer& s) {
    s.write(MessageType::request_virtual_key_toggle_notification);
    s.write(name);
  });
}

bool ClientPort::send_set_instance_id(std::string_view id) {
  return m_connection.send_message([&](Serializer& s) {
    s.write(MessageType::set_instance_id);
    s.write(id);
  });
}

bool ClientPort::send_set_config_file(const std::string& filename) {
  return m_connection.send_message([&](Serializer& s) {
    s.write(MessageType::set_config_file);
    s.write(filename);
  });
}

bool ClientPort::send_request_next_key_info() {
  return m_connection.send_message([&](Serializer& s) {
    s.write(MessageType::next_key_info);
  });
}

bool ClientPort::send_inject_input(const std::string& string) {
  return m_connection.send_message([&](Serializer& s) {
    s.write(MessageType::inject_input);
    s.write(static_cast<uint32_t>(string.size()));
    s.write(string.data(), string.size());
  });
}

bool ClientPort::send_inject_output(const std::string& string) {
  return m_connection.send_message([&](Serializer& s) {
    s.write(MessageType::inject_output);
    s.write(static_cast<uint32_t>(string.size()));
    s.write(string.data(), string.size());
  });
}

bool ClientPort::send_type_string(const std::string& string) {
  return m_connection.send_message([&](Serializer& s) {
    s.write(MessageType::inject_output);
    s.write(static_cast<uint32_t>(string.size() + 2));
    s.write('"');
    s.write(string.data(), string.size());
    s.write('"');
  });
}

bool ClientPort::send_notify(const std::string& string) {
  return m_connection.send_message([&](Serializer& s) {
    s.write(MessageType::notify);
    s.write(static_cast<uint32_t>(string.size()));
    s.write(string.data(), string.size());
  });
}

bool ClientPort::read_virtual_key_state(std::optional<Duration> timeout, 
    std::optional<KeyState>* result) {
  return m_connection.read_messages(timeout,
    [&](Deserializer& d) {
      switch (d.read<MessageType>()) {
        case MessageType::virtual_key_state: {
          d.read<Key>();
          const auto state = d.read<KeyState>();
          result->emplace(state);
          break;
        }
        default: 
          break;
      }
    });
}

bool ClientPort::read_next_key_info(std::optional<Duration> timeout, 
    std::string* result) {
  return m_connection.read_messages(timeout,
    [&](Deserializer& d) {
      switch (d.read<MessageType>()) {
        case MessageType::next_key_info: {
          if (result)
            *result = d.read_string();
          break;
        }
        default: 
          break;
      }
    });
}
