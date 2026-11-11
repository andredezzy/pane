// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/lock_triggers.h"

#include <windows.h>

#include <memory>
#include <utility>

#include "ui/base/win/power_setting_change_listener.h"

namespace pane {

namespace {

class WinSignals;

// The one set of signals, for the foreground hook's callback, which takes no
// context of its own.
WinSignals* g_signals = nullptr;

// Windows': its displays turning off, which ui/base's power setting listener
// hears; another user taking the session locks it first, which
// ui::AddScreenLockCallback hears. Pane leaving the front, from the system's
// foreground event, with no input hook.
class WinSignals : public LockTriggers::Signals,
                   public ui::PowerSettingChangeListener {
 public:
  WinSignals(base::RepeatingClosure lock, base::RepeatingClosure left_front)
      : lock_(std::move(lock)),
        left_front_(std::move(left_front)),
        display_(this) {
    g_signals = this;
    foreground_ = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
        &WinSignals::ForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT);
  }

  ~WinSignals() override {
    if (foreground_) {
      UnhookWinEvent(foreground_);
    }
    g_signals = nullptr;
  }

  // LockTriggers::Signals:
  bool InFront() const override {
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
  }

  // ui::PowerSettingChangeListener:
  void OnDisplayStateChanged(bool display_on) override {
    if (!display_on) {
      lock_.Run();
    }
  }
  // The computer's sleep is base::PowerMonitor's.
  void OnResume() override {}
  void OnSuspend() override {}

 private:
  static void CALLBACK ForegroundChanged(HWINEVENTHOOK hook,
                                         DWORD event,
                                         HWND window,
                                         LONG object,
                                         LONG child,
                                         DWORD thread,
                                         DWORD time) {
    if (g_signals && !g_signals->InFront()) {
      g_signals->left_front_.Run();
    }
  }

  const base::RepeatingClosure lock_;
  const base::RepeatingClosure left_front_;
  ui::ScopedPowerSettingChangeListener display_;
  HWINEVENTHOOK foreground_ = nullptr;
};

}  // namespace

// static
std::unique_ptr<LockTriggers::Signals> LockTriggers::Signals::Create(
    base::RepeatingClosure lock,
    base::RepeatingClosure left_front) {
  return std::make_unique<WinSignals>(std::move(lock), std::move(left_front));
}

}  // namespace pane
