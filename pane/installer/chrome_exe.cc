// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/chrome_exe.h"

#include <windows.h>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/path_service.h"
#include "base/process/process.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "pane/installer/switches.h"

namespace pane::installer {

base::FilePath ChromeIn(const base::FilePath& app) {
  return app.Append(L"chrome.exe");
}

base::FilePath UserFolder() {
  base::FilePath home;
  base::PathService::Get(base::DIR_HOME, &home);
  return home;
}

std::optional<int> RunChrome(const base::FilePath& app,
                             const std::vector<std::string>& switches,
                             base::TimeDelta timeout,
                             base::FunctionRef<void(const std::string&)> note,
                             const base::LaunchOptions& extra) {
  base::CommandLine command(ChromeIn(app));
  for (const std::string& value : switches) {
    const size_t equals = value.find('=');
    if (equals == std::string::npos) {
      command.AppendSwitch(value);
    } else {
      command.AppendSwitchASCII(value.substr(0, equals),
                                value.substr(equals + 1));
    }
  }
  base::LaunchOptions options = extra;
  options.start_hidden = true;
  options.feedback_cursor_off = true;
  options.current_directory = UserFolder();
  note(
      base::StrCat({"run ", base::WideToUTF8(command.GetCommandLineString())}));
  base::Process process = base::LaunchProcess(command, options);
  if (!process.IsValid()) {
    note(base::StringPrintf("could not start chrome.exe: %lu",
                            ::GetLastError()));
    return std::nullopt;
  }
  int code = -1;
  if (!process.WaitForExitWithTimeout(timeout, &code)) {
    note("chrome.exe did not end in time");
    process.Terminate(1, false);
    return std::nullopt;
  }
  note(base::StringPrintf("chrome.exe ended: 0x%08x",
                          static_cast<unsigned>(code)));
  return code;
}

std::optional<int> RegisterWithWindows(
    const base::FilePath& app,
    bool first_install,
    base::FunctionRef<void(const std::string&)> note) {
  return RunChrome(
      app,
      {base::StrCat({kRegisterSwitch, "=",
                     first_install ? kRegisterFirst : kRegisterRepair})},
      kRegisterTimeout, note);
}

}  // namespace pane::installer
