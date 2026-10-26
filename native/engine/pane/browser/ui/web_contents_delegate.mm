// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's ACWebContentsDelegate (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.mm, Copyright 2026 Mantra Patel,
// Apache License 2.0; native/third_party/ardia).

#include "pane/browser/ui/web_contents_delegate.h"

#include <utility>

#include "base/auto_reset.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/ui/browser_window.h"
#include "pane/browser/ui/tab_bridge.h"
#include "pane/browser/ui/window_host.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"

namespace pane {

namespace {

bool IsWindowDisposition(WindowOpenDisposition disposition) {
  return disposition == WindowOpenDisposition::NEW_POPUP ||
         disposition == WindowOpenDisposition::NEW_WINDOW;
}

bool IsNewTabDisposition(WindowOpenDisposition disposition) {
  return IsWindowDisposition(disposition) ||
         disposition == WindowOpenDisposition::NEW_FOREGROUND_TAB ||
         disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB ||
         disposition == WindowOpenDisposition::NEW_SPLIT_VIEW;
}

}  // namespace

HostedWebContentsDelegate::HostedWebContentsDelegate(
    BrowserWindowInterface* browser,
    ExclusiveAccessManager& exclusive_access_manager,
    chrome::BrowserCommandController& command_controller,
    UnloadController& unload_controller,
    web_app::AppBrowserController* app_browser_controller,
    BrowserWindow& window,
    DesktopBrowserWindowCapabilities& capabilities,
    BrowserUiController& browser_ui_controller,
    base::WeakPtr<WindowHost> host)
    : BrowserWebContentsDelegate(browser,
                                 exclusive_access_manager,
                                 command_controller,
                                 unload_controller,
                                 app_browser_controller,
                                 window,
                                 capabilities,
                                 browser_ui_controller),
      host_(std::move(host)),
      ours_(browser) {}

HostedWebContentsDelegate::~HostedWebContentsDelegate() {
  HostedBrowserWindow::Forget(ours_);
}

content::WebContents* HostedWebContentsDelegate::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  WindowHost* host = host_.get();
  if (!host || !IsNewTabDisposition(params.disposition)) {
    return BrowserWebContentsDelegate::OpenURLFromTab(
        source, params, std::move(navigation_handle_callback));
  }

  content::OpenURLParams tab_params(params);
  base::AutoReset<WindowHost::PendingOpen> pending(
      &host->pending_open(),
      WindowHost::PendingOpen{params.disposition,
                              source ? source->GetWeakPtr() : nullptr});
  if (IsWindowDisposition(params.disposition)) {
    tab_params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  } else if (params.disposition == WindowOpenDisposition::NEW_SPLIT_VIEW) {
    tab_params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  }
  return BrowserWebContentsDelegate::OpenURLFromTab(
      source, tab_params, std::move(navigation_handle_callback));
}

content::WebContents* HostedWebContentsDelegate::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  if (host_ && new_contents) {
    TabBridge::GetOrCreate(new_contents.get())->set_open(disposition, source);
    if (IsWindowDisposition(disposition)) {
      disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    }
  }
  return BrowserWebContentsDelegate::AddNewContents(
      source, std::move(new_contents), target_url, disposition,
      window_features, user_gesture, was_blocked);
}

}  // namespace pane
