// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_WINDOWS_INTEGRATION_H_
#define PANE_APP_WINDOWS_INTEGRATION_H_

#include <windows.h>

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "pane/model/app_settings.h"

class RegistryEntry;

namespace installer {
class TranslationDelegate;
}

namespace pane::windows {

// Pane on Windows' own surfaces (ADR 0127; Paper 11.12.3 to 11.12.5,
// 11.12.16): its Start entry and desktop shortcut, Default apps, Installed
// apps, the toast activator; all under HKCU, for this user alone, through
// Chromium's own ShellUtil and the seam it keeps for an installer's words.
// Windows' alone (windows_integration_win.cc).

// Pane's words where Chromium's installer strings reach Windows: the
// shortcuts' name and tooltip, Default apps' name and description, the
// publisher (Chromium's installer::SetTranslationDelegate seam).
installer::TranslationDelegate& PanesWords();

// Pane's words for this process, in place of Chromium's own (the browser's
// delegate, set before this one).
void UsePanesWords();

// The App icon's file for `sky` in the app folder: AppIcon-<Sky>.ico,
// make-icons.swift's (11.12.16).
base::FilePath SkyIcon(const base::FilePath& app_folder, model::AppIcon sky);

// The sky the shortcuts name as the data's settings have it: the App icon
// setting, Automatic taking Day or Night by Pane's Theme, and under System
// by Windows' apps' light or dark. Read from settings.json as it is, plain
// and changed by nothing; the default where there is none.
model::AppIcon ShortcutSky(const base::FilePath& data_folder);

// Default apps' registry entries for `chrome_exe` (11.12.5), on Chromium's
// model (shell_util.cc's ProgIDs, Capabilities, StartMenuInternet,
// RegisteredApplications, OpenWithProgids), without App Paths: Pane's names,
// its icon files, its description.
std::vector<std::unique_ptr<RegistryEntry>> DefaultAppsEntries(
    const base::FilePath& chrome_exe);

// Registers Pane with Windows for `chrome_exe` (the register switch): the
// toast activator, Default apps, the uninstall entry, Start's shortcut, and,
// at a first install, the desktop's (11.12.3, call A), the shortcuts naming
// the sky `data_folder`'s settings give (call D). ERROR_SUCCESS, or the
// failed step's error.
DWORD Register(const base::FilePath& chrome_exe,
               const base::FilePath& data_folder,
               bool first_install);

// Start's shortcut to `chrome_exe`, in the folder base::DIR_START_MENU names,
// made as Register makes it: Pane's name, its AppUserModelID, the toast
// activator, and the sky `data_folder`'s settings give. Windows' toasts are
// Chrome's only while it is there (NotificationPlatformBridgeWin reads it as
// a profile opens). False if it couldn't be made.
bool MakeStartShortcut(const base::FilePath& chrome_exe,
                       const base::FilePath& data_folder);

// Pane's AppUserModelID, which its window, shortcuts, ProgIDs, and toasts
// carry. It reads the command line, which Chrome's start changes on the UI
// thread (its flags' switches): read it there, and hand it to another.
std::wstring AppUserModelId();

// Takes every toast of `app_id`, Pane's AppUserModelID, out of Windows'
// notification center: those Chrome shows a site's under
// (NotificationPlatformBridgeWin), an earlier Pane's that crashed included.
// As Pane starts, before any profile, at Exit, and as Windows ends the
// session with Pane open (ADR 0109).
// Blocks, about 10 ms (18.5 to 49.5 at a launch in the test VM, its first use
// of Windows' notifications there); on a thread COM is initialized on, the
// browser's thread pool's among them. False if Windows' center refused.
bool ClearToasts(const std::wstring& app_id);

// How an erase is going: the data's bytes erased, of all of them.
using EraseProgress = base::RepeatingCallback<void(uint64_t, uint64_t)>;

// What Unregister gives where Pane holds its data: it is open.
inline constexpr DWORD kDataInUse = ERROR_LOCK_VIOLATION;

// Removes every registration of `chrome_exe`'s: both shortcuts and any pin
// naming it, Default apps' entries, the uninstall entry, the toast activator,
// and HKCU\Software\Pane but, with the data kept, its preference MACs, which
// are the data's (11.12.9, 11.12.10). With `erase`, first the data, the
// panic wipe's way (ADR 0042, 0113): the TPM's keys and the data key go
// first, so nothing left on disk reads, then every file of %LOCALAPPDATA%\Pane
// is overwritten and deleted, the preference MACs with them. kDataInUse
// where a Pane holds the data, before anything is changed. The uninstall
// entry goes last, and only where everything else went: a step that failed
// leaves it, so Installed apps can run the uninstall again.
DWORD Unregister(const base::FilePath& chrome_exe,
                 const base::FilePath& data_folder,
                 bool erase,
                 const EraseProgress& progress);

// What PointShortcutsAt did: the shortcuts it rewrote, and those it found
// and couldn't read or rewrite. Another program may hold one: Windows' shell
// maps Start's shortcut while it reads it for Pane's taskbar button, and an
// overwrite meanwhile fails with ERROR_USER_MAPPED_FILE (W10a).
struct PointedShortcuts {
  int rewritten = 0;
  int failed = 0;
};

// Points Pane's shortcuts at `sky`'s icon (call D, 11.12.16): Start's and
// the desktop's, and each taskbar pin whose target is `chrome_exe`, never
// one that names another app; Windows told of each it rewrote
// (SHChangeNotify). Each already naming it is left as it is.
PointedShortcuts PointShortcutsAt(const base::FilePath& chrome_exe,
                                  model::AppIcon sky);

}  // namespace pane::windows

#endif  // PANE_APP_WINDOWS_INTEGRATION_H_
