// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_PANE_MAIN_DELEGATE_H_
#define PANE_APP_PANE_MAIN_DELEGATE_H_

#include <memory>

#include "chrome/app/chrome_main_delegate.h"  // nogncheck: lives in chrome_dll

struct StartupTimestamps;

namespace pane {

// Chrome's main delegate with Pane's clients plugged in. chrome/app's
// ChromeMain builds this instead of a plain ChromeMainDelegate, so every
// process of PaneCore.framework runs through it.
class PaneMainDelegate : public ChromeMainDelegate {
 public:
  explicit PaneMainDelegate(const StartupTimestamps& timestamps);
  PaneMainDelegate(const PaneMainDelegate&) = delete;
  PaneMainDelegate& operator=(const PaneMainDelegate&) = delete;
  ~PaneMainDelegate() override;

 protected:
  // ChromeMainDelegate:
  content::ContentBrowserClient* CreateContentBrowserClient() override;
};

std::unique_ptr<ChromeMainDelegate> CreateMainDelegate(
    const StartupTimestamps& timestamps);

}  // namespace pane

#endif  // PANE_APP_PANE_MAIN_DELEGATE_H_
