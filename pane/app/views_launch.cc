// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/views_launch.h"

#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
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
  std::unique_ptr<base::Environment> environment = base::Environment::Create();
  const std::string data = environment->GetVar("PANE_DATA_DIR").value_or("");
  if (!command_line.HasSwitch("use-mock-keychain") || data.empty()) {
    LOG(ERROR) << "Pane's Views UI runs only as a test copy for now: "
                  "--use-mock-keychain and PANE_DATA_DIR";
    return kRefused;
  }

  SetEngineSwitches(command_line, base::FilePath::FromUTF8Unsafe(data));
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
