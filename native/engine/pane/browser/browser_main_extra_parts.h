// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_BROWSER_MAIN_EXTRA_PARTS_H_
#define PANE_BROWSER_BROWSER_MAIN_EXTRA_PARTS_H_

#include "chrome/browser/chrome_browser_main_extra_parts.h"

namespace pane {

// Tells the Pane app when a hosted engine is ready, and releases the app's
// profiles and pages before Chrome tears profiles down. Process lifetime needs
// nothing extra: Chrome's AppController already keeps the browser process
// alive without windows on macOS, and releases that keep-alive when quitting.
class BrowserMainExtraParts : public ChromeBrowserMainExtraParts {
 public:
  BrowserMainExtraParts();
  BrowserMainExtraParts(const BrowserMainExtraParts&) = delete;
  BrowserMainExtraParts& operator=(const BrowserMainExtraParts&) = delete;
  ~BrowserMainExtraParts() override;

  // ChromeBrowserMainExtraParts:
  void PostBrowserStart() override;
  void PostMainMessageLoopRun() override;
};

}  // namespace pane

#endif  // PANE_BROWSER_BROWSER_MAIN_EXTRA_PARTS_H_
