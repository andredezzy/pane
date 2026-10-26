// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's TabBridge (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.{h,mm}, Copyright 2026 Mantra
// Patel, Apache License 2.0; native/third_party/ardia).

#ifndef PANE_BROWSER_UI_TAB_BRIDGE_H_
#define PANE_BROWSER_UI_TAB_BRIDGE_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "components/favicon/core/favicon_driver_observer.h"
#include "components/find_in_page/find_result_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ui/base/window_open_disposition.h"

@class PaneWebContents;

namespace pane {

// Ties one tab's WebContents to the PaneWebContents the app holds, and turns
// Chromium's per-tab callbacks into the app's delegate calls. Lives as the
// WebContents' user data, so it goes with the tab.
class TabBridge : public content::WebContentsObserver,
                  public favicon::FaviconDriverObserver,
                  public find_in_page::FindResultObserver,
                  public content::WebContentsUserData<TabBridge> {
 public:
  TabBridge(const TabBridge&) = delete;
  TabBridge& operator=(const TabBridge&) = delete;
  ~TabBridge() override;

  static TabBridge* GetOrCreate(content::WebContents* contents);

  PaneWebContents* tab() const { return tab_; }

  // Starts the observers that need the tab helpers Chrome attaches when the
  // tab joins a tab strip (favicons, find in page).
  void Observe();

  // How a page opened this tab, recorded before it reaches the tab strip.
  void set_open(WindowOpenDisposition disposition,
                content::WebContents* source);
  std::optional<WindowOpenDisposition> TakeOpenDisposition();
  content::WebContents* open_source() const { return open_source_.get(); }

  // Chrome's tab id: stable for the tab's life, and the id extensions see.
  int tab_id() const;
  void TraceCallback(std::string_view name, base::DictValue args = {});

  // content::WebContentsObserver:
  void TitleWasSet(content::NavigationEntry* entry) override;
  void PrimaryPageChanged(content::Page& page) override;
  void NavigationEntryCommitted(
      const content::LoadCommittedDetails& load_details) override;
  void NavigationListPruned(
      const content::PrunedDetails& pruned_details) override;
  void NavigationEntriesDeleted() override;
  void DidStartLoading() override;
  void DidStopLoading() override;
  void LoadProgressChanged(double progress) override;
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;

  // favicon::FaviconDriverObserver:
  void OnFaviconUpdated(favicon::FaviconDriver* favicon_driver,
                        NotificationIconType notification_icon_type,
                        const GURL& icon_url,
                        bool icon_url_changed,
                        const gfx::Image& image) override;

  // find_in_page::FindResultObserver:
  void OnFindResultAvailable(content::WebContents* web_contents) override;
  void OnFindTabHelperDestroyed(find_in_page::FindTabHelper* helper) override;

 private:
  friend class content::WebContentsUserData<TabBridge>;
  explicit TabBridge(content::WebContents* contents);

  void NavigationStateChanged();

  PaneWebContents* __strong tab_;
  std::optional<WindowOpenDisposition> open_disposition_;
  base::WeakPtr<content::WebContents> open_source_;
  bool observing_favicon_ = false;
  bool observing_find_ = false;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace pane

#endif  // PANE_BROWSER_UI_TAB_BRIDGE_H_
