// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's ACWebContentsDelegate (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.{h,mm}, Copyright 2026 Mantra
// Patel, Apache License 2.0; native/third_party/ardia).

#ifndef PANE_BROWSER_UI_WEB_CONTENTS_DELEGATE_H_
#define PANE_BROWSER_UI_WEB_CONTENTS_DELEGATE_H_

#include <memory>

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

  // Chrome's dialogs and menus for the tab, shown by the app (PaneDialogs.h).
  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
      content::WebContents* source) override;
  void ShowRepostFormWarningDialog(content::WebContents* source) override;
  bool HandleContextMenu(content::RenderFrameHost& render_frame_host,
                         const content::ContextMenuParams& params) override;
  void RendererUnresponsive(
      content::WebContents* source,
      content::RenderWidgetHost* render_widget_host,
      base::RepeatingClosure hang_monitor_restarter) override;
  void RendererResponsive(
      content::WebContents* source,
      content::RenderWidgetHost* render_widget_host) override;
  // Screen capture (getDisplayMedia) is declined without Chrome's picker,
  // as in the Electron app; camera and microphone go to the permission prompt.
  void RequestMediaAccessPermission(
      content::WebContents* web_contents,
      const content::MediaStreamRequest& request,
      content::MediaResponseCallback callback) override;

 private:
  base::WeakPtr<WindowHost> host_;
};

}  // namespace pane

#endif  // PANE_BROWSER_UI_WEB_CONTENTS_DELEGATE_H_
