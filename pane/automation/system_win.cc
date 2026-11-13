// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/system.h"

#include <windows.h>

#include "ui/gfx/win/singleton_hwnd.h"

namespace pane {

// Windows' time zone is the system's alone: the engine's monitor and ICU read
// it from the registry when Windows says it changed (time_zone_monitor_win.cc,
// ICU's uprv_detectWindowsTimeZone), with no TZ to read first, so no zone
// changes for Pane alone without changing the computer's.
bool ChangeSystemTimeZone(const std::string& zone) {
  return false;
}

// What a screen reader asks of the application on Windows is UI Automation's,
// W8's with Narrator (ADR 0071); no scenario that runs there asks for it.
void RequestEnhancedUserInterface() {}

bool PostSystemEvent(const std::string& event) {
  // Sent to Pane's own window for the system's messages, as Windows sends
  // them: the user's apps hear none of it.
  const HWND hwnd = gfx::SingletonHwnd::GetInstance()->hwnd();
  if (event == "screensSleep") {
    // The displays turning off (GUID_SESSION_DISPLAY_STATUS, a DWORD whose
    // low byte ui::PowerSettingChangeListener reads), which fits in the
    // setting's own padding.
    POWERBROADCAST_SETTING setting = {};
    setting.PowerSetting = GUID_SESSION_DISPLAY_STATUS;
    setting.DataLength = sizeof(DWORD);
    setting.Data[0] = static_cast<UCHAR>(PowerMonitorOff);
    ::SendMessage(hwnd, WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE,
                  reinterpret_cast<LPARAM>(&setting));
    return true;
  }
  if (event == "sessionResign") {
    // Another user taking the session locks it first, which
    // ui::AddScreenLockCallback hears.
    DWORD session = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &session);
    ::SendMessage(hwnd, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, session);
    return true;
  }
  return false;
}

// AppleScript is the Mac's (ADR 0112).
base::expected<std::optional<std::string>, std::string> RunAppleScript(
    const std::string& source) {
  return base::unexpected("Windows runs no AppleScript");
}

}  // namespace pane
