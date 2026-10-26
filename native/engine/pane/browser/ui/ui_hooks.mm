// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/ui/ui_hooks.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/devtools/devtools_contents_resizing_strategy.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "components/permissions/chooser_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/extensions/extension_ui.h"
#include "pane/browser/ui/browser_window.h"
#include "pane/browser/ui/login_handler.h"
#include "pane/browser/ui/permission_prompt.h"
#include "pane/browser/ui/select_file_dialog.h"
#include "pane/browser/ui/tab_bridge.h"
#include "url/gurl.h"

#import "pane/browser/ui/dialogs_internal.h"
#import "pane/public/PaneWebContents.h"

namespace pane {

namespace {

PaneWebContents* TabFor(content::WebContents* web_contents) {
  TabBridge* bridge =
      web_contents ? TabBridge::FromWebContents(web_contents) : nullptr;
  return bridge ? bridge->tab() : nil;
}

bool RunExternalProtocolDialog(
    const GURL& url,
    content::WebContents* web_contents,
    const std::optional<url::Origin>& initiating_origin,
    content::WeakDocumentPtr initiator_document,
    const std::u16string& program_name) {
  PaneWebContents* tab = TabFor(web_contents);
  id<PaneWebContentsDelegate> delegate = tab.delegate;
  if (![delegate respondsToSelector:@selector(webContents:
                                       openExternalProtocol:)]) {
    return false;
  }
  PaneExternalProtocolRequest* request = [[PaneExternalProtocolRequest alloc]
          initWithURL:url
      applicationName:program_name
            originURL:initiating_origin ? initiating_origin->GetURL() : GURL()
                  tab:tab
               answer:base::BindOnce(
                          [](GURL url,
                             base::WeakPtr<content::WebContents> contents,
                             content::WeakDocumentPtr initiator_document,
                             bool open) {
                            if (open && contents) {
                              ExternalProtocolHandler::
                                  LaunchUrlWithoutSecurityCheck(
                                      url, contents.get(),
                                      std::move(initiator_document));
                            }
                          },
                          url, web_contents->GetWeakPtr(),
                          std::move(initiator_document))];
  TabBridge::FromWebContents(web_contents)
      ->TraceCallback("PaneWebContentsDelegate.openExternalProtocol",
                      base::DictValue().Set("url", url.spec()));
  [delegate webContents:tab openExternalProtocol:request];
  return true;
}

bool HostsDevTools(BrowserWindowInterface* browser) {
  return HostedBrowserWindow::IsOurs(browser);
}

// Docked DevTools appeared, changed, or went: the app places the DevTools
// page (DevToolsWindow keeps it while docked) beside the inspected tab.
void UpdateDevTools(BrowserWindowInterface* browser,
                    content::WebContents* inspected) {
  TabBridge* bridge = TabBridge::FromWebContents(inspected);
  if (!bridge) {
    return;
  }
  DevToolsContentsResizingStrategy strategy;
  content::WebContents* devtools =
      DevToolsWindow::GetInTabWebContents(inspected, &strategy);
  PaneWebContents* devtools_tab =
      devtools ? TabBridge::GetOrCreate(devtools)->tab() : nil;
  // Chrome lays the page over the DevTools view at these bounds (the
  // splitter); empty when DevTools covers it.
  const gfx::Rect page_bounds = strategy.hide_inspected_contents()
                                    ? gfx::Rect()
                                    : strategy.bounds();
  if (devtools_tab == bridge->devtools() &&
      page_bounds == bridge->devtools_page_bounds()) {
    return;
  }
  bridge->set_devtools(devtools_tab, page_bounds);
  bridge->TraceCallback("PaneWebContentsDelegate.didChangeDevTools",
                        base::DictValue().Set("open", devtools != nullptr));
  PaneWebContents* tab = bridge->tab();
  id<PaneWebContentsDelegate> delegate = tab.delegate;
  if ([delegate respondsToSelector:@selector(webContents:
                                       didChangeDevTools:)]) {
    [delegate webContents:tab didChangeDevTools:devtools_tab];
  }
}

void WillPrint(content::WebContents* web_contents) {
  TabBridge* bridge = TabBridge::FromWebContents(web_contents);
  if (!bridge) {
    return;
  }
  bridge->TraceCallback("PaneWebContentsDelegate.willPrint");
  PaneWebContents* tab = bridge->tab();
  id<PaneWebContentsDelegate> delegate = tab.delegate;
  if ([delegate respondsToSelector:@selector(webContentsWillPrint:)]) {
    [delegate webContentsWillPrint:tab];
  }
}

// Device choosers stay closed, as in the Electron app: the page's
// requestDevice() is declined at once, with no UI.
bool DeclineDeviceChooser(
    content::RenderFrameHost* owner,
    std::unique_ptr<permissions::ChooserController>& controller) {
  TabBridge* bridge = TabBridge::FromWebContents(
      content::WebContents::FromRenderFrameHost(owner));
  if (!bridge) {
    return false;
  }
  bridge->TraceCallback("policy.deviceChooserDeclined",
                        base::DictValue().Set(
                            "origin",
                            owner->GetLastCommittedOrigin().Serialize()));
  controller->Cancel();
  controller.reset();
  return true;
}

}  // namespace

void InstallUIHooks() {
  g_pane_create_permission_prompt = &PermissionPrompt::Create;
  g_pane_create_login_handler = &LoginHandler::Create;
  g_pane_external_protocol_dialog = &RunExternalProtocolDialog;
  g_pane_hosts_devtools = &HostsDevTools;
  g_pane_update_devtools = &UpdateDevTools;
  g_pane_will_print = &WillPrint;
  g_pane_device_chooser = &DeclineDeviceChooser;
  InstallExtensionHooks();
  InstallSelectFileDialogFactory();
}

}  // namespace pane
