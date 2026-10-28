// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's ACWebContentsDelegate (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.mm, Copyright 2026 Mantra Patel,
// Apache License 2.0; native/third_party/ardia).

#include "pane/browser/app_callback.h"
#include "pane/browser/ui/web_contents_delegate.h"

#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/media_stream_request.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/result_codes.h"
#include "pane/browser/ui/browser_window.h"
#include "pane/browser/ui/javascript_dialog_manager.h"
#include "pane/browser/ui/tab_bridge.h"
#include "pane/browser/ui/window_host.h"
#include "third_party/blink/public/common/mediastream/media_stream_request.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"

#import "pane/browser/ui/dialogs_internal.h"
#import "pane/public/PaneWebContents.h"

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

content::JavaScriptDialogManager*
HostedWebContentsDelegate::GetJavaScriptDialogManager(
    content::WebContents* source) {
  return JavaScriptDialogManager::GetInstance();
}

void HostedWebContentsDelegate::ShowRepostFormWarningDialog(
    content::WebContents* source) {
  JavaScriptDialogManager::GetInstance()->RunFormResubmissionDialog(source);
}

bool HostedWebContentsDelegate::HandleContextMenu(
    content::RenderFrameHost& render_frame_host,
    const content::ContextMenuParams& params) {
  content::WebContents* contents =
      content::WebContents::FromRenderFrameHost(&render_frame_host);
  TabBridge* bridge = TabBridge::FromWebContents(contents);
  PaneWebContents* tab = bridge ? bridge->tab() : nil;
  id<PaneWebContentsDelegate> delegate = tab.delegate;
  if (![delegate respondsToSelector:@selector(webContents:showContextMenu:)]) {
    return false;
  }
  PaneContextMenu* menu =
      [[PaneContextMenu alloc] initWithParams:params frame:render_frame_host];
  PANE_TRACE_TAB(
      bridge, "PaneWebContentsDelegate.showContextMenu",
      base::DictValue()
          .Set("x", params.x)
          .Set("y", params.y)
          .Set("link", params.link_url.spec())
          .Set("editable", params.is_editable)
          .Set("misspelled", base::UTF16ToUTF8(params.misspelled_word))
          .Set("spellcheck", params.spellcheck_enabled)
          .Set("suggestions",
               static_cast<int>(params.dictionary_suggestions.size())));
  ScopedAppCallback callback;
  [delegate webContents:tab showContextMenu:menu];
  return true;
}

void HostedWebContentsDelegate::RendererUnresponsive(
    content::WebContents* source,
    content::RenderWidgetHost* render_widget_host,
    base::RepeatingClosure hang_monitor_restarter) {
  TabBridge* bridge = TabBridge::FromWebContents(source);
  PaneWebContents* tab = bridge ? bridge->tab() : nil;
  id<PaneWebContentsDelegate> delegate = tab.delegate;
  if (![delegate respondsToSelector:@selector(webContents:
                                       becameUnresponsive:)]) {
    // Nobody to ask: keep waiting, as Chrome does until the user decides.
    return;
  }
  if (bridge->unresponsive_page() && !bridge->unresponsive_page().answered) {
    return;
  }
  PaneUnresponsivePage* page = [[PaneUnresponsivePage alloc]
      initWithTab:tab
           answer:base::BindOnce(
                      [](content::ChildProcessId process,
                         base::RepeatingClosure restart_hang_monitor,
                         bool terminate) {
                        if (!terminate) {
                          restart_hang_monitor.Run();
                        } else if (content::RenderProcessHost* host =
                                       content::RenderProcessHost::FromID(
                                           process)) {
                          host->Shutdown(content::RESULT_CODE_HUNG);
                        }
                      },
                      render_widget_host->GetProcess()->GetID(),
                      std::move(hang_monitor_restarter))];
  bridge->set_unresponsive_page(page);
  PANE_TRACE_TAB(bridge, "PaneWebContentsDelegate.becameUnresponsive");
  ScopedAppCallback callback;
  [delegate webContents:tab becameUnresponsive:page];
}

void HostedWebContentsDelegate::RendererResponsive(
    content::WebContents* source,
    content::RenderWidgetHost* render_widget_host) {
  if (TabBridge* bridge = TabBridge::FromWebContents(source)) {
    [bridge->unresponsive_page() withdraw];
    bridge->set_unresponsive_page(nil);
  }
}

void HostedWebContentsDelegate::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  if (blink::IsScreenCaptureMediaType(request.video_type)) {
    if (TabBridge* bridge = TabBridge::FromWebContents(web_contents)) {
      PANE_TRACE_TAB(bridge, "policy.screenCaptureDeclined");
    }
    std::move(callback).Run(blink::mojom::StreamDevicesSet(),
                            blink::mojom::MediaStreamRequestResult::NOT_SUPPORTED,
                            nullptr);
    return;
  }
  BrowserWebContentsDelegate::RequestMediaAccessPermission(
      web_contents, request, std::move(callback));
}

}  // namespace pane
