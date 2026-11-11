// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's TabBridge (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.{h,mm}, Copyright 2026 Mantra
// Patel, Apache License 2.0; third_party/ardia).

#ifndef PANE_BROWSER_UI_HOSTED_TAB_H_
#define PANE_BROWSER_UI_HOSTED_TAB_H_

#include <optional>
#include <string_view>

#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "pane/browser/engine_trace.h"
#include "ui/base/window_open_disposition.h"

namespace pane {

// What Pane keeps of each tab of the Browsers its windows host (WindowHost),
// from the moment the tab joins one: how a page opened it, for the window to
// tell Pane's UI; its id in traces; and the Chrome Web Store's buttons, which
// say Pane (ADR 0076). Lives as the WebContents' user data, so it goes with
// the tab; the page Chrome puts in a discarded one's place gets its own.
class HostedTab : public content::WebContentsObserver,
                  public content::WebContentsUserData<HostedTab> {
 public:
  HostedTab(const HostedTab&) = delete;
  HostedTab& operator=(const HostedTab&) = delete;
  ~HostedTab() override;

  static HostedTab* GetOrCreate(content::WebContents* contents);

  // How a page opened this tab, recorded before it reaches the tab strip.
  void set_open(WindowOpenDisposition disposition,
                content::WebContents* source);
  std::optional<WindowOpenDisposition> TakeOpenDisposition();
  content::WebContents* open_source() const { return open_source_.get(); }

  // Chrome's tab id: stable for the tab's life, and the id extensions see.
  int tab_id() const;

  // Use through PANE_TRACE_TAB, which builds `args` only while tracing.
  void TraceCallback(std::string_view name, base::DictValue args = {});

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

 private:
  friend class content::WebContentsUserData<HostedTab>;
  explicit HostedTab(content::WebContents* contents);

  std::optional<WindowOpenDisposition> open_disposition_;
  base::WeakPtr<content::WebContents> open_source_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace pane

// Traces a call or callback about `tab`'s tab, with its tab id (Trace's line
// plus "tab"). `tab` and `args` are evaluated only while tracing.
#define PANE_TRACE_TAB(tab, name, ...)                       \
  do {                                                       \
    if (::pane::IsTracing()) {                               \
      (tab)->TraceCallback(name __VA_OPT__(, ) __VA_ARGS__); \
    }                                                        \
  } while (0)

#endif  // PANE_BROWSER_UI_HOSTED_TAB_H_
