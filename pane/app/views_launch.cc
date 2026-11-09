// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/views_launch.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "pane/app/data_launch.h"
#include "pane/app/engine_switches.h"
#include "pane/app/test_copy.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/ui/install_hooks.h"

namespace pane {

namespace {

bool g_views_launch = false;

// What a refused launch exits with, as Chrome's own refusals do.
constexpr int kRefused = 1;

}  // namespace

std::optional<int> StartViewsLaunch(base::CommandLine& command_line) {
  // A scenario names its data folder, or a home of its own (CFFIXED_USER_HOME),
  // whose Application Support holds Pane's data as a Mac's does; either lies in
  // the temporary folder, as the runner makes them, so this launch never
  // writes beside the user's data (test_copy.h).
  const base::FilePath data = DataDirectory();
  if (!command_line.HasSwitch("use-mock-keychain") ||
      !InTemporaryFolder(data)) {
    LOG(ERROR) << "Pane's Views UI runs only as a test copy for now: "
                  "--use-mock-keychain, and a data folder in the temporary "
                  "folder (PANE_DATA_DIR, or a home of its own)";
    return kRefused;
  }

  // What main.swift did before Chromium ran, in its order, and then the
  // engine.
  if (std::optional<int> exit_code = PrepareData(data, command_line)) {
    return exit_code;
  }
  SetEngineSwitches(command_line, data);
  g_views_launch = true;
  AllowTracing();
  // Before the first Browser exists: Pane's windows host Chrome's Browsers.
  InstallBrowserHooks();
  return std::nullopt;
}

bool IsViewsLaunch() {
  return g_views_launch;
}

}  // namespace pane
