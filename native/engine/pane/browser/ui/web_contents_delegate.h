// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's ACWebContentsDelegate (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.{h,mm}, Copyright 2026 Mantra
// Patel, Apache License 2.0; native/third_party/ardia).

#ifndef PANE_BROWSER_UI_WEB_CONTENTS_DELEGATE_H_
#define PANE_BROWSER_UI_WEB_CONTENTS_DELEGATE_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.h"

namespace pane {

class WindowHost;

// The WebContentsDelegate of Pane's Browsers: Chrome's own, except that a
// page's popups and new windows (window.open, target=_blank, ⌘-click, POST
// forms) become tabs of the same Browser, keeping Chrome's new WebContents
// and its navigation (so POST bodies and openers survive). The app hears of
// them as inserted tabs, with their opener.
class HostedWebContentsDelegate : public BrowserWebContentsDelegate {
 public:
  HostedWebContentsDelegate(
      BrowserWindowInterface* browser,
      ExclusiveAccessManager& exclusive_access_manager,
      chrome::BrowserCommandController& command_controller,
      UnloadController& unload_controller,
      web_app::AppBrowserController* app_browser_controller,
      BrowserWindow& window,
      DesktopBrowserWindowCapabilities& capabilities,
      BrowserUiController& browser_ui_controller,
      base::WeakPtr<WindowHost> host);
  HostedWebContentsDelegate(const HostedWebContentsDelegate&) = delete;
  HostedWebContentsDelegate& operator=(const HostedWebContentsDelegate&) =
      delete;
  ~HostedWebContentsDelegate() override;

  // BrowserWebContentsDelegate:
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override;

 private:
  base::WeakPtr<WindowHost> host_;
  // The Browser this delegate serves, forgotten as Pane's when it goes
  // (after its window, in ~Browser).
  raw_ptr<const BrowserWindowInterface> ours_;
};

}  // namespace pane

#endif  // PANE_BROWSER_UI_WEB_CONTENTS_DELEGATE_H_
