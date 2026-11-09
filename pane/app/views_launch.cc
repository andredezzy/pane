// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/views_launch.h"

#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "pane/app/engine_switches.h"
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
  // whose Application Support holds Pane's data as the Swift app's does;
  // either lies in the temporary folder, as the runner makes them, so this
  // launch never writes beside the user's data.
  std::unique_ptr<base::Environment> environment = base::Environment::Create();
  const base::FilePath named = base::FilePath::FromUTF8Unsafe(
      environment->GetVar("PANE_DATA_DIR").value_or(""));
  const base::FilePath home = base::FilePath::FromUTF8Unsafe(
      environment->GetVar("CFFIXED_USER_HOME").value_or(""));
  // What must lie in the temporary folder: the data folder named, or the
  // home the data folder is derived in.
  const base::FilePath root = named.empty() ? home : named;
  const base::FilePath data =
      named.empty() ? home.Append(FILE_PATH_LITERAL("Library"))
                          .Append(FILE_PATH_LITERAL("Application Support"))
                          .Append(FILE_PATH_LITERAL("xyz.roundedcorners.pane"))
                    : named;
  base::FilePath temp;
  const bool throwaway = !root.empty() && base::GetTempDir(&temp) &&
                         base::MakeAbsoluteFilePath(temp).IsParent(
                             base::MakeAbsoluteFilePath(root));
  if (!command_line.HasSwitch("use-mock-keychain") || !throwaway) {
    LOG(ERROR) << "Pane's Views UI runs only as a test copy for now: "
                  "--use-mock-keychain, and a data folder in the temporary "
                  "folder (PANE_DATA_DIR, or a home of its own)";
    return kRefused;
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
