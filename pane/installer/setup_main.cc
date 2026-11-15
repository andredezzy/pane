// Copyright 2026 Pane. Licensed under GPL-3.0-only.

// Pane's installer, Pane-<version>-x64.exe, and the helper its app folder
// keeps, pane_setup.exe (ADR 0127): the same program, which installs the
// payload package-win.sh appended to it, or, started by chrome.exe
// --pane-uninstall from a copy outside the app folder, shows Pane's
// uninstall dialog.

#include <objbase.h>

#include <windows.h>

#include <optional>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/logging/logging_settings.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "pane/installer/flow.h"
#include "pane/installer/payload.h"
#include "pane/installer/switches.h"

int WINAPI wWinMain(HINSTANCE instance,
                    HINSTANCE previous,
                    wchar_t* command_line,
                    int show) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(0, nullptr);
  // Nothing beside the installer, nor a debug.log beside the helper: the
  // system's debug log alone, and the installer's own notes on a failure.
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  logging::InitLogging(settings);
  // Each monitor's own scale, as Pane's window has it.
  ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  const base::CommandLine& switches = *base::CommandLine::ForCurrentProcess();
  if (switches.HasSwitch(pane::installer::kUninstallDialogSwitch)) {
    uint64_t after = 0;
    base::StringToUint64(
        switches.GetSwitchValueASCII(pane::installer::kAfterSwitch), &after);
    pane::installer::UninstallFlow flow(
        switches.GetSwitchValuePath(pane::installer::kAppSwitch),
        static_cast<base::ProcessId>(after));
    return flow.Run();
  }

  base::FilePath installer;
  base::PathService::Get(base::FILE_EXE, &installer);
  std::optional<pane::installer::Payload> payload;
  base::File self(installer, base::File::FLAG_OPEN | base::File::FLAG_READ |
                                 base::File::FLAG_WIN_SHARE_DELETE);
  if (self.IsValid()) {
    payload = pane::installer::FindPayload(self);
  }
  pane::installer::InstallFlow flow(installer, std::move(payload));
  return flow.Run();
}
