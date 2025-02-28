
#include "TrayIcon.h"
#include "common/output.h"

using MakeTrayIconImpl = std::unique_ptr<TrayIcon::IImpl>();
MakeTrayIconImpl make_tray_icon_gtk;
MakeTrayIconImpl make_tray_icon_cocoa;

TrayIcon::TrayIcon() = default;
TrayIcon::TrayIcon(TrayIcon&& rhs) noexcept = default;
TrayIcon& TrayIcon::operator=(TrayIcon&& rhs) noexcept = default;
TrayIcon::~TrayIcon() = default;

void TrayIcon::initialize(Handler* handler, bool show_reload) {
  if (m_impl)
    return;

#if defined(ENABLE_APPINDICATOR)
  if (auto impl = make_tray_icon_gtk()) 
    if (impl->initialize(handler, show_reload)) {
      m_impl = std::move(impl);
      verbose("Initialized GTK tray icon");
    }
#elif defined(ENABLE_COCOA)
  if (auto impl = make_tray_icon_cocoa()) 
    if (impl->initialize(handler, show_reload)) {
      m_impl = std::move(impl);
      verbose("Initialized Cocoa tray icon");
    }
#endif
}

void TrayIcon::reset() {
  m_impl.reset();
}

void TrayIcon::update() {
  if (m_impl)
    m_impl->update();
}
