// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/engine_switches.h"

#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "pane/common/host.h"

namespace pane {

namespace {

// Adds `value` to a comma-separated switch (--disable-features=…), keeping
// whatever the command line already lists there.
void AddToSwitchList(base::CommandLine& command_line,
                     std::string_view name,
                     std::string_view value) {
  std::string list = command_line.GetSwitchValueASCII(name);
  list += list.empty() ? "" : ",";
  list += value;
  command_line.RemoveSwitch(name);
  command_line.AppendSwitchASCII(name, list);
}

}  // namespace

void SetEngineSwitches(base::CommandLine& command_line,
                       const base::FilePath& data_directory) {
  if (!command_line.HasSwitch("use-mock-keychain")) {
    base::CommandLine kept(command_line.GetProgram());
    if (command_line.HasSwitch("pane-wipe-after")) {
      kept.AppendSwitchNative(
          "pane-wipe-after",
          command_line.GetSwitchValueNative("pane-wipe-after"));
    }
    for (const base::CommandLine::StringType& arg : command_line.GetArgs()) {
      kept.AppendArgNative(arg);
    }
    command_line = kept;
  }

  // Chrome's own UI stays off: Pane draws every window and the menu bar.
  if (!command_line.HasSwitch(PANE_HOSTED_SWITCH)) {
    command_line.AppendSwitch(PANE_HOSTED_SWITCH);
  }
  command_line.AppendSwitch("no-startup-window");
  command_line.AppendSwitch("no-first-run");
  // Printing goes straight to the system print panel, not Chrome's preview.
  command_line.AppendSwitch("disable-print-preview");
  // From 158, a data directory other than Chrome's default, as Pane's is, can't
  // use the "v10" key every cookie and password was sealed under: they read as
  // gone, and every site signs out. It guards one app's data from another's
  // copy of Chrome; Pane's are behind its own key already (docs/adr/0121).
  AddToSwitchList(command_line, "disable-features",
                  "KeychainKeyProviderUserDataDirProtection");
  // Features that put Chrome's own UI outside Pane's window: Glic (Gemini in
  // Chrome) adds a menu bar item, the Media Router a Cast dialog (the
  // Electron app had no casting).
  AddToSwitchList(command_line, "disable-features", "Glic,MediaRouter");
  // Requests to Google that Chrome makes on its own and the Electron app
  // never did (found by their traffic annotations in a net log): the
  // component updater, the network time check, and the on-device AI model
  // go out from the browser itself, past every profile's proxy; AI Mode's
  // eligibility check carries a profile's Google cookies and country; the
  // Translate ranker fetches its model. The default search engine's
  // preconnect opens connections to Google no page asked for, at start and
  // again while a page shows, which a net log shows as connections, not
  // requests.
  command_line.AppendSwitch("disable-component-update");
  AddToSwitchList(command_line, "disable-features",
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
  AddToSwitchList(command_line, "disable-features",
                  "SegmentationPlatform,SegmentationPlatformUkmEngine");
  // Chromium's own encrypted copies of the bookmarks and the sessions open
  // with the Keychain's secret alone, without the PIN: the profile's own files
  // replace them (ADR 0121).
  AddToSwitchList(command_line, "disable-features",
                  "EncryptBookmarks,EncryptSessionStorage");
  // Autofill AI's Google Wallet passes, which come only with the browser
  // sign-in and autofill each profile turns off (profiles.cc): with them on,
  // every page Chrome makes asks LocalAuthentication whether this Mac can
  // authenticate its user (ChromeAutofillClient's constructor), a synchronous
  // call to the system's daemon that held the UI thread about 10 ms of each
  // new tab's or woken tab's page.
  AddToSwitchList(command_line, "disable-features",
                  "AutofillAiWalletPrivatePasses");
  // Its histograms (BrowserMetrics) Chrome sets up before it reads the
  // features: patch 0008 keeps them off.
  // Chrome discards background tabs under memory pressure (its urgent
  // discarding), swapping an empty WebContents into the tab. Pane's sleep
  // owns that job, so Chrome's stays off. An extension's chrome.tabs.discard
  // still works, and Pane's UI follows the tab into its new WebContents.
  AddToSwitchList(command_line, "enable-features", "DisableTabDiscarding");
  // Pane's own views raster on the CPU, in the browser, as AppKit drew the
  // Swift app's and as pane/ui/render.cc paints a render; pages still raster
  // on the GPU. Rastered on the GPU (Skia Graphite, on Dawn), the window's
  // views brought the GPU process the pipelines a cold launch compiles before
  // its first frame, the threads that compile them, and the caches that draw
  // them: the window's first frame came 32 ms later, and the GPU process held
  // about 8 MB more at rest (ADR 0123, docs/benchmarks/views-shell.md).
  AddToSwitchList(command_line, "disable-features", "UiGpuRasterization");
  // Pane's views shape their text with HarfBuzz over each font table where
  // CoreText keeps it, not over a copy: gfx copies every table a typeface's
  // HarfBuzz face reads into the browser's heap, and the sizes and weights of
  // the system font the UI draws make many typefaces of it: at one tab 11 of
  // them held a copy each of SF's GPOS and GDEF, 7.8 MB, where the Swift's
  // CoreText read the font's own (ADR 0125). The tables' bytes are the same,
  // so the text shapes and draws the same.
  AddToSwitchList(command_line, "enable-features", "HarfBuzzZeroCopyFontTable");

  // An explicit --user-data-dir (scenarios use throwaway ones) wins.
  if (!command_line.HasSwitch("user-data-dir")) {
    command_line.AppendSwitchPath(
        "user-data-dir", data_directory.Append(FILE_PATH_LITERAL("Engine")));
  }
}

}  // namespace pane
