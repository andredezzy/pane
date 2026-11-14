// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/views_launch.h"

#include <string>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "pane/app/data_launch.h"
#include "pane/app/engine_switches.h"
#include "pane/app/test_copy.h"
#include "pane/browser/ui/install_hooks.h"
#include "pane/model/engine_folder.h"

namespace pane {

namespace {

bool g_views_launch = false;

// What a refused launch exits with, as Chrome's own refusals do.
constexpr int kRefused = 1;

}  // namespace

std::optional<int> StartViewsLaunch(base::CommandLine& command_line) {
  // The user's data, or a test copy's on the mock keychain (test_copy.h).
  // PANE_DATA_DIR is read here whether or not the hooks are on, so a test copy
  // started without the switch is known by it too.
  const LaunchKind kind =
      KindOfLaunch(command_line.HasSwitch("use-mock-keychain"),
                   base::FilePath::FromUTF8Unsafe(base::Environment::Create()
                                                      ->GetVar("PANE_DATA_DIR")
                                                      .value_or(std::string())),
                   DefaultDataDirectory());
  switch (kind) {
    case LaunchKind::kUser:
    case LaunchKind::kTestCopy:
      break;
    case LaunchKind::kTestCopyWithoutMockKeychain:
      LOG(ERROR) << "Pane refused a test copy without --use-mock-keychain: its "
                    "data lies in the temporary folder";
      return kRefused;
    case LaunchKind::kMockKeychainOutsideTest:
      LOG(ERROR) << "Pane refused --use-mock-keychain on data outside the "
                    "temporary folder";
      return kRefused;
  }

  // What main.swift did before Chromium ran, in its order, and then the
  // engine.
  const base::FilePath data = DataDirectory();
  if (std::optional<int> exit_code =
          PrepareData(data, command_line,
                      /*test_copy=*/kind == LaunchKind::kTestCopy)) {
    return exit_code;
  }
  SetEngineSwitches(command_line, model::EngineFolder(data));
  g_views_launch = true;
  // Before the first Browser exists: Pane's windows host Chrome's Browsers.
  InstallBrowserHooks();
  return std::nullopt;
}

bool IsViewsLaunch() {
  return g_views_launch;
}

}  // namespace pane
