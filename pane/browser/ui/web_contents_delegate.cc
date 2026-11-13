// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's ACWebContentsDelegate
// (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.mm, Copyright 2026 Mantra Patel,
// Apache License 2.0; third_party/ardia).

#include "pane/browser/ui/web_contents_delegate.h"

#include <utility>

#include "base/auto_reset.h"
#include "content/public/browser/media_stream_request.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/ui/browser_window.h"
#include "pane/browser/ui/hosted_tab.h"
#include "pane/browser/ui/window_host.h"
#include "pane/ui/dialogs/javascript_dialogs.h"
#include "pane/ui/dialogs/unresponsive_page.h"
#include "third_party/blink/public/common/mediastream/media_stream_request.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"

namespace pane {

namespace {

bool IsWindowDisposition(WindowOpenDisposition disposition) {
  return disposition == WindowOpenDisposition::kNewPopup ||
         disposition == WindowOpenDisposition::kNewWindow;
}

bool IsNewTabDisposition(WindowOpenDisposition disposition) {
  return IsWindowDisposition(disposition) ||
         disposition == WindowOpenDisposition::kNewForegroundTab ||
         disposition == WindowOpenDisposition::kNewBackgroundTab ||
         disposition == WindowOpenDisposition::kNewSplitView;
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
      host_(std::move(host)) {}

HostedWebContentsDelegate::~HostedWebContentsDelegate() = default;

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
    tab_params.disposition = WindowOpenDisposition::kNewForegroundTab;
  } else if (params.disposition == WindowOpenDisposition::kNewSplitView) {
    tab_params.disposition = WindowOpenDisposition::kNewBackgroundTab;
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
    HostedTab::GetOrCreate(new_contents.get())->set_open(disposition, source);
    if (IsWindowDisposition(disposition)) {
      disposition = WindowOpenDisposition::kNewForegroundTab;
    }
  }
  return BrowserWebContentsDelegate::AddNewContents(
      source, std::move(new_contents), target_url, disposition, window_features,
      user_gesture, was_blocked);
}

content::JavaScriptDialogManager*
HostedWebContentsDelegate::GetJavaScriptDialogManager(
    content::WebContents* source) {
  return JavaScriptDialogs::Get();
}

void HostedWebContentsDelegate::ShowRepostFormWarningDialog(
    content::WebContents* source) {
  JavaScriptDialogs::Get()->RunFormResubmissionDialog(source);
}

// The page's menu is the window's (pane/ui/page/page_context_menu.h):
// Chrome's own, filtered, shown by Pane; Chrome's view shows none.
bool HostedWebContentsDelegate::HandleContextMenu(
    content::RenderFrameHost& render_frame_host,
    const content::ContextMenuParams& params) {
  return host_ &&
         host_->delegate().HandleContextMenu(render_frame_host, params);
}

void HostedWebContentsDelegate::RendererUnresponsive(
    content::WebContents* source,
    content::RenderWidgetHost* render_widget_host,
    base::RepeatingClosure hang_monitor_restarter) {
  AskAboutUnresponsivePage(source, render_widget_host,
                           std::move(hang_monitor_restarter));
}

void HostedWebContentsDelegate::RendererResponsive(
    content::WebContents* source,
    content::RenderWidgetHost* render_widget_host) {
  PageResponsiveAgain(source);
}

void HostedWebContentsDelegate::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  if (blink::IsScreenCaptureMediaType(request.video_type)) {
    if (HostedTab* tab = HostedTab::FromWebContents(web_contents)) {
      PANE_TRACE_TAB(tab, "policy.screenCaptureDeclined");
    }
    std::move(callback).Run(
        blink::mojom::StreamDevicesSet(),
        blink::mojom::MediaStreamRequestResult::NOT_SUPPORTED, nullptr);
    return;
  }
  BrowserWebContentsDelegate::RequestMediaAccessPermission(
      web_contents, request, std::move(callback));
}

}  // namespace pane
