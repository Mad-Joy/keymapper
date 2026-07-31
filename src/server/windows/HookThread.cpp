
#include "HookThread.h"
#include "common/output.h"
#include <thread>
#include <future>
#include <mutex>

namespace {
  constexpr UINT WM_APP_CONFIGURE_HOOKS = WM_APP + 0;

  struct HookConfig {
    HINSTANCE instance;
    KeyboardHookCallback keyboard_callback;
    MouseHookCallback mouse_callback;
  };

  HookConfig g_hook_config;
  HHOOK g_keyboard_hook;
  HHOOK g_mouse_hook;
  std::thread g_hook_thread;
  DWORD g_hook_thread_id;
  std::mutex g_next_hook_config_mutex;
  HookConfig g_next_hook_config;

  LRESULT CALLBACK keyboard_hook_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
      const auto& kbd = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
      if (g_hook_config.keyboard_callback(wparam, kbd))
        return 1;
    }
    return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
  }

  LRESULT CALLBACK mouse_hook_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
      const auto& ms = *reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
      if (g_hook_config.mouse_callback(wparam, ms))
        return 1;
    }
    return CallNextHookEx(g_mouse_hook, code, wparam, lparam);
  }

  void update_hook_config() {
    const auto lock = std::lock_guard(g_next_hook_config_mutex);
    g_hook_config = g_next_hook_config;
  }

  void unhook_devices() {
    if (g_keyboard_hook)
      UnhookWindowsHookEx(g_keyboard_hook);
    g_keyboard_hook = nullptr;

    if (g_mouse_hook)
      UnhookWindowsHookEx(g_mouse_hook);
    g_mouse_hook = nullptr;
  }

  void hook_devices() {
    const auto keyboard_was_hooked = (g_keyboard_hook != nullptr);
    const auto mouse_was_hooked = (g_mouse_hook != nullptr);

    unhook_devices();
    update_hook_config();

    if (g_hook_config.keyboard_callback)
      g_keyboard_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL, keyboard_hook_proc, g_hook_config.instance, 0);

    if (g_hook_config.mouse_callback)
      g_mouse_hook = SetWindowsHookExW(
        WH_MOUSE_LL, mouse_hook_proc, g_hook_config.instance, 0);

    const auto keyboard_is_hooked = (g_keyboard_hook != nullptr);
    const auto mouse_is_hooked = (g_mouse_hook != nullptr);
    if (keyboard_is_hooked != keyboard_was_hooked)
      verbose(keyboard_is_hooked ? "Hooked keyboard" : "Unhooked keyboard");
    if (mouse_is_hooked != mouse_was_hooked)
      verbose(mouse_is_hooked ? "Hooked mouse" : "Unhooked mouse");
  }

  void hook_thread_main(std::promise<void> ready) {
    g_hook_thread_id = GetCurrentThreadId();

    // create message queue
    auto message = MSG{ };
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    ready.set_value();

    while (GetMessageW(&message, nullptr, 0, 0) > 0)
      if (message.message == WM_APP_CONFIGURE_HOOKS)
        hook_devices();

    unhook_devices();
  }

  void start_hook_thread() {
    auto ready = std::promise<void>();
    auto ready_future = ready.get_future();
    g_hook_thread = std::thread(hook_thread_main, std::move(ready));
    ready_future.get();
  }

  void post_hook_config(const HookConfig& config) {
    const auto lock = std::lock_guard(g_next_hook_config_mutex);
    g_next_hook_config = config;
    PostThreadMessageW(g_hook_thread_id, WM_APP_CONFIGURE_HOOKS, 0, 0);
  }
} // namespace

void unhook_devices() {
  if (g_hook_thread.joinable())
    post_hook_config({ });
}

void hook_devices(HINSTANCE instance,
    KeyboardHookCallback keyboard_hook_callback,
    MouseHookCallback mouse_hook_callback) {
  if (!g_hook_thread.joinable())
    start_hook_thread();
  post_hook_config({ instance, keyboard_hook_callback, mouse_hook_callback });
}

void shutdown_hook_thread() {
  if (g_hook_thread.joinable()) {
    PostThreadMessageW(g_hook_thread_id, WM_QUIT, 0, 0);
    g_hook_thread.join();
  }
}
