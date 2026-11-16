// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/install_switches.h"

#include <windows.h>

#include <string>
#include <utility>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/win/scoped_com_initializer.h"
#include "pane/app/data_launch.h"
#include "pane/app/windows_integration.h"
#include "pane/installer/files.h"
#include "pane/installer/switches.h"

namespace pane {

namespace {

// A failed step's code as the installer shows it, an HRESULT.
int Failed(DWORD error) {
  return static_cast<int>(HRESULT_FROM_WIN32(error));
}

// Copies the helper out of the app folder into a folder of its own in the
// temporary folder, which it deletes as it exits, and starts it on Pane's
// uninstall dialog (Paper 11.12.9): the app folder can only go once nothing
// runs from it, this chrome.exe included.
int StartUninstallDialog() {
  base::FilePath exe;
  base::FilePath temp;
  if (!base::PathService::Get(base::FILE_EXE, &exe) ||
      !base::GetTempDir(&temp)) {
    return Failed(ERROR_PATH_NOT_FOUND);
  }
  const base::FilePath app = exe.DirName();
  const std::optional<base::FilePath> helper = installer::CopyHelperOut(
      app.Append(installer::kHelperName), installer::kUninstallCopyPrefix);
  if (!helper) {
    const DWORD error = ::GetLastError();
    LOG(ERROR) << "Could not copy the uninstall's helper: " << error;
    return Failed(error ? error : ERROR_FILE_NOT_FOUND);
  }
  base::CommandLine command(*helper);
  command.AppendSwitch(installer::kUninstallDialogSwitch);
  command.AppendSwitchPath(installer::kAppSwitch, app);
  command.AppendSwitchASCII(installer::kAfterSwitch,
                            base::NumberToString(base::GetCurrentProcId()));
  base::LaunchOptions options;
  options.current_directory = temp;
  options.grant_foreground_privilege = true;
  if (!base::LaunchProcess(command, options).IsValid()) {
    const DWORD error = ::GetLastError();
    LOG(ERROR) << "Could not start the uninstall's helper: " << error;
    return Failed(error ? error : ERROR_GEN_FAILURE);
  }
  return installer::kSwitchDone;
}

// The pipe the helper reads the erase's progress from, a line at a time:
// "<erased> <total>", at most every 50 ms and at its end.
windows::EraseProgress ProgressTo(const base::CommandLine& command_line) {
  uint64_t value = 0;
  if (!base::StringToUint64(
          command_line.GetSwitchValueASCII(installer::kProgressSwitch),
          &value)) {
    return windows::EraseProgress();
  }
  HANDLE pipe = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
  if (::GetFileType(pipe) != FILE_TYPE_PIPE) {
    return windows::EraseProgress();
  }
  return base::BindRepeating(
      [](HANDLE pipe, base::TimeTicks* last, uint64_t erased, uint64_t total) {
        const base::TimeTicks now = base::TimeTicks::Now();
        if (erased < total && now - *last < base::Milliseconds(50)) {
          return;
        }
        *last = now;
        const std::string line =
            base::StrCat({base::NumberToString(erased), " ",
                          base::NumberToString(total), "\n"});
        DWORD written = 0;
        ::WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()),
                    &written, nullptr);
      },
      pipe, base::Owned(new base::TimeTicks()));
}

}  // namespace

std::optional<int> RunInstallSwitch(const base::CommandLine& command_line) {
  const bool registers = command_line.HasSwitch(installer::kRegisterSwitch);
  const bool unregisters = command_line.HasSwitch(installer::kUnregisterSwitch);
  const bool uninstalls = command_line.HasSwitch(installer::kUninstallSwitch);
  if (!registers && !unregisters && !uninstalls) {
    return std::nullopt;
  }
  if (uninstalls) {
    return StartUninstallDialog();
  }
  // Only the install's own chrome.exe registers Pane or takes it off, never
  // one in another folder (out/Win, a user's copy), whose registrations
  // would name it, or whose unregister would take the install's own away.
  base::FilePath chrome_exe;
  if (!base::PathService::Get(base::FILE_EXE, &chrome_exe) ||
      !installer::IsInstalledAppFolder(chrome_exe.DirName())) {
    LOG(ERROR) << "Not the install's chrome.exe: " << chrome_exe;
    return Failed(ERROR_PATH_NOT_FOUND);
  }
  // Shortcuts and pins go through the shell's COM objects.
  base::win::ScopedCOMInitializer com;
  windows::UsePanesWords();
  const base::FilePath data = DefaultDataDirectory();
  if (registers) {
    const DWORD error = windows::Register(
        chrome_exe, data,
        command_line.GetSwitchValueASCII(installer::kRegisterSwitch) ==
            installer::kRegisterFirst);
    return error ? Failed(error) : installer::kSwitchDone;
  }
  const DWORD error = windows::Unregister(
      chrome_exe, data,
      command_line.GetSwitchValueASCII(installer::kUnregisterSwitch) ==
          installer::kUnregisterErase,
      ProgressTo(command_line));
  if (error == windows::kDataInUse) {
    return installer::kSwitchPaneOpen;
  }
  return error ? Failed(error) : installer::kSwitchDone;
}

void FirstWindowPainted() {
  static bool told = false;
  if (std::exchange(told, true)) {
    return;
  }
  const std::wstring name =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueNative(
          installer::kInstalledSwitch);
  // Only an installer's event, by its name.
  if (!base::StartsWith(name, installer::kInstalledEventPrefix)) {
    return;
  }
  if (HANDLE event = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str())) {
    ::SetEvent(event);
    ::CloseHandle(event);
  }
}

}  // namespace pane
