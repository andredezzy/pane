// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/engine_switches.h"

#include <algorithm>
#include <string_view>

#include "base/files/file_path.h"
#include "pane/common/host.h"

namespace pane {

namespace {

bool HasSwitch(const std::vector<std::string>& args, std::string_view name) {
  const std::string prefix = "--" + std::string(name);
  return std::ranges::any_of(args, [&](const std::string& arg) {
    return arg == prefix || arg.starts_with(prefix + "=");
  });
}

// Adds `value` to a comma-separated switch (--disable-features=…), keeping
// whatever the command line already lists there.
void AddToSwitchList(std::vector<std::string>& args,
                     std::string_view name,
                     std::string_view value) {
  const std::string prefix = "--" + std::string(name) + "=";
  for (std::string& arg : args) {
    if (arg.starts_with(prefix)) {
      arg += "," + std::string(value);
      return;
    }
  }
  args.push_back(prefix + std::string(value));
}

}  // namespace

std::vector<std::string> EngineCommandLine(
    const std::vector<std::string>& argv,
    const base::FilePath& data_directory) {
  const bool scenario =
      std::ranges::find(argv, "--use-mock-keychain") != argv.end();
  std::vector<std::string> args;
  for (size_t index = 0; index < argv.size(); index++) {
    const std::string& arg = argv[index];
    if (index > 0 && !scenario && arg.starts_with("-") &&
        !arg.starts_with("--pane-wipe-after=")) {
      continue;
    }
    args.push_back(arg);
  }

  // Chrome's own UI stays off: Pane draws every window and the menu bar.
  if (!HasSwitch(args, PANE_HOSTED_SWITCH)) {
    args.push_back("--" PANE_HOSTED_SWITCH);
  }
  args.push_back("--no-startup-window");
  args.push_back("--no-first-run");
  // Printing goes straight to the system print panel, not Chrome's preview.
  args.push_back("--disable-print-preview");
  // From 158, a data directory other than Chrome's default, as Pane's is, can't
  // use the "v10" key every cookie and password was sealed under: they read as
  // gone, and every site signs out. It guards one app's data from another's
  // copy of Chrome; Pane's are behind its own key already (docs/adr/0121).
  AddToSwitchList(args, "disable-features",
                  "KeychainKeyProviderUserDataDirProtection");
  // Features that put Chrome's own UI outside Pane's window: Glic (Gemini in
  // Chrome) adds a menu bar item, the Media Router a Cast dialog (the
  // Electron app had no casting).
  AddToSwitchList(args, "disable-features", "Glic,MediaRouter");
  // Requests to Google that Chrome makes on its own and the Electron app
  // never did (found by their traffic annotations in a net log): the
  // component updater, the network time check, and the on-device AI model
  // go out from the browser itself, past every profile's proxy; AI Mode's
  // eligibility check carries a profile's Google cookies and country; the
  // Translate ranker fetches its model. The default search engine's
  // preconnect opens connections to Google no page asked for, at start and
  // again while a page shows, which a net log shows as connections, not
  // requests.
  args.push_back("--disable-component-update");
  AddToSwitchList(args, "disable-features",
                  "NetworkTimeServiceQuerying,"
                  "AimEnabled,"
                  "OptimizationGuideModelExecution,"
                  "OnDeviceModelBackgroundDownload,OptimizationHints,"
                  "TranslateRankerQuery,TranslateRankerEnforcement,"
                  "PreconnectToSearch");
  // The segmentation platform keeps a database of the pages visited
  // (segmentation_platform/ukm_db) at the user data dir's root, outside the
  // volume every profile's data is in (ADR 0113), for guesses Chrome's own UI
  // makes, which Pane does not show.
  AddToSwitchList(args, "disable-features",
                  "SegmentationPlatform,SegmentationPlatformUkmEngine");
  // Chromium's own encrypted copies of the bookmarks and the sessions open
  // with the Keychain's secret alone, without the PIN: the profile's own files
  // replace them (ADR 0121).
  AddToSwitchList(args, "disable-features",
                  "EncryptBookmarks,EncryptSessionStorage");
  // Its histograms (BrowserMetrics) Chrome sets up before it reads the
  // features: patch 0008 keeps them off.
  // Chrome discards background tabs under memory pressure (its urgent
  // discarding), swapping an empty WebContents into the tab. Pane's sleep
  // owns that job, so Chrome's stays off. An extension's chrome.tabs.discard
  // still works, and the TabBridge follows the tab into its new WebContents.
  AddToSwitchList(args, "enable-features", "DisableTabDiscarding");

  // An explicit --user-data-dir (scenarios use throwaway ones) wins.
  if (!HasSwitch(args, "user-data-dir")) {
    args.push_back("--user-data-dir=" +
                   data_directory.Append("Engine").value());
  }
  return args;
}

}  // namespace pane
