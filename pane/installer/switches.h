// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_SWITCHES_H_
#define PANE_INSTALLER_SWITCHES_H_

namespace pane::installer {

// The switches between Pane's installer, its helper (the same program,
// copied out of the app folder), and Pane's chrome.exe (ADR 0127;
// pane/app/install_switches_win.cc handles chrome.exe's).

// chrome.exe: registers Pane with Windows (Start's and the desktop's
// shortcuts, Default apps, the uninstall entry, the toast activator), then
// exits: "first" at a first install, which makes the desktop's shortcut;
// "repair" at a reinstall, which leaves it where the user deleted it.
inline constexpr char kRegisterSwitch[] = "pane-register";
inline constexpr char kRegisterFirst[] = "first";
inline constexpr char kRegisterRepair[] = "repair";

// chrome.exe: removes every registration, then exits; "erase" erases the
// data the panic wipe's way, "keep" keeps it.
inline constexpr char kUnregisterSwitch[] = "pane-unregister";
inline constexpr char kUnregisterKeep[] = "keep";
inline constexpr char kUnregisterErase[] = "erase";
// Where the erase writes how far it has gone: an inherited pipe's handle.
inline constexpr char kProgressSwitch[] = "pane-progress";

// chrome.exe, Installed apps' UninstallString: copies the helper out of the
// app folder and starts it with kUninstallDialogSwitch, then exits.
inline constexpr char kUninstallSwitch[] = "pane-uninstall";

// The helper: Pane's uninstall dialog, for the app folder kAppSwitch names,
// once the process kAfterSwitch names has exited.
inline constexpr char kUninstallDialogSwitch[] = "pane-uninstall-dialog";
inline constexpr char kAppSwitch[] = "pane-app";
inline constexpr char kAfterSwitch[] = "pane-after";

// chrome.exe, the first launch after an install: the named event it sets
// once its first window has painted, which the install window waits for
// (Paper 11.12.6, "it closes as Pane's window opens").
inline constexpr char kInstalledSwitch[] = "pane-installed";
inline constexpr wchar_t kInstalledEventPrefix[] = L"Local\\Pane installer ";

// What chrome.exe's switches exit with: done, or Pane holds its data (it is
// open); any other code is the failed step's HRESULT.
inline constexpr int kSwitchDone = 0;
inline constexpr int kSwitchPaneOpen = 3;

// The helper's name in the app folder.
inline constexpr wchar_t kHelperName[] = L"pane_setup.exe";

}  // namespace pane::installer

#endif  // PANE_INSTALLER_SWITCHES_H_
