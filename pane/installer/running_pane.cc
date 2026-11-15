// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/running_pane.h"

#include <windows.h>

#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/process/process.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "base/win/scoped_handle.h"
#include "pane/installer/files.h"

namespace pane::installer {

namespace {

// Chromium's process singleton's window class (chrome/common/
// chrome_constants.cc's kMessageWindowClass), which install modes don't
// rename.
constexpr wchar_t kSingletonClass[] = L"Chrome_MessageWindow";

// What Pane's AppEvents hears in a handed command line (pane/ui/app/
// app_events.h's kAskToExitSwitch).
constexpr wchar_t kAskToExit[] = L"--pane-exit";

// How long a busy Pane has to take the command line: Chromium's own wait
// (chrome_process_finder.cc's notification timeout).
constexpr UINT kAnswerMilliseconds = 20000;

// The process's executable, as Windows names it, or nothing.
base::FilePath ExecutableOf(base::ProcessId pid) {
  base::win::ScopedHandle process(
      ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
  if (!process.is_valid()) {
    return base::FilePath();
  }
  std::array<wchar_t, 32768> path = {};
  DWORD length = static_cast<DWORD>(path.size());
  if (!::QueryFullProcessImageNameW(process.get(), 0, path.data(), &length)) {
    return base::FilePath();
  }
  return base::FilePath(std::wstring_view(path.data(), length));
}

}  // namespace

std::vector<base::ProcessId> ProcessesFrom(const base::FilePath& folder,
                                           base::ProcessId except) {
  std::vector<base::ProcessId> found;
  base::win::ScopedHandle snapshot(
      ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (!snapshot.is_valid()) {
    return found;
  }
  PROCESSENTRY32W entry = {.dwSize = sizeof(entry)};
  for (BOOL more = ::Process32FirstW(snapshot.get(), &entry); more;
       more = ::Process32NextW(snapshot.get(), &entry)) {
    const base::ProcessId pid = entry.th32ProcessID;
    if (pid == 0 || pid == except || pid == base::GetCurrentProcId()) {
      continue;
    }
    if (IsInFolder(ExecutableOf(pid), folder)) {
      found.push_back(pid);
    }
  }
  return found;
}

std::vector<HWND> PanesFrom(const base::FilePath& folder) {
  std::vector<HWND> panes;
  for (HWND window =
           ::FindWindowExW(HWND_MESSAGE, nullptr, kSingletonClass, nullptr);
       window; window = ::FindWindowExW(HWND_MESSAGE, window, kSingletonClass,
                                        nullptr)) {
    DWORD pid = 0;
    if (::GetWindowThreadProcessId(window, &pid) &&
        IsInFolder(ExecutableOf(pid), folder)) {
      panes.push_back(window);
    }
  }
  return panes;
}

bool AskToExit(HWND pane) {
  DWORD pid = 0;
  if (!::GetWindowThreadProcessId(pane, &pid)) {
    return false;
  }
  base::FilePath current;
  base::GetCurrentDirectory(&current);
  // Chromium's own message (AttemptToNotifyRunningChrome):
  // "START\0<current directory>\0<command line>\0", the command line that of
  // a launch of the browser itself with the one switch.
  const base::FilePath browser = ExecutableOf(pid);
  const std::wstring command_line =
      base::StrCat({L"\"", browser.value(), L"\" ", kAskToExit});
  const std::wstring message = base::StrCat(
      {std::wstring_view(L"START\0", 6), current.value(),
       std::wstring_view(L"\0", 1), command_line, std::wstring_view(L"\0", 1)});
  // Pane may bring its window forward to ask.
  ::AllowSetForegroundWindow(pid);
  COPYDATASTRUCT data = {
      .dwData = 0,
      .cbData = static_cast<DWORD>((message.size() + 1) * sizeof(wchar_t)),
      .lpData = const_cast<wchar_t*>(message.c_str())};
  DWORD_PTR answer = 0;
  return ::SendMessageTimeoutW(
             pane, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
             SMTO_ABORTIFHUNG, kAnswerMilliseconds, &answer) &&
         answer != 0;
}

void EndProcesses(const std::vector<base::ProcessId>& processes) {
  for (base::ProcessId pid : processes) {
    base::Process::OpenWithAccess(pid, PROCESS_TERMINATE)
        .Terminate(/*exit_code=*/1, /*wait=*/false);
  }
}

bool WaitForExit(const std::vector<base::ProcessId>& processes,
                 base::TimeDelta timeout) {
  const base::TimeTicks deadline = base::TimeTicks::Now() + timeout;
  for (base::ProcessId pid : processes) {
    const base::Process process =
        base::Process::OpenWithAccess(pid, SYNCHRONIZE);
    if (!process.IsValid()) {
      continue;
    }
    if (!process.WaitForExitWithTimeout(
            std::max(deadline - base::TimeTicks::Now(), base::TimeDelta()),
            nullptr)) {
      return false;
    }
  }
  return true;
}

}  // namespace pane::installer
