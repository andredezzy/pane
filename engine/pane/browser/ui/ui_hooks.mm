// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/app_callback.h"
#include "pane/browser/ui/ui_hooks.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/extensions/extension_ui.h"
#include "pane/browser/ui/browser_window.h"
#include "pane/browser/ui/login_handler.h"
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
  TabBridge* bridge = TabBridge::FromWebContents(web_contents);
  bridge->WithdrawWhenClosed(request);
  PANE_TRACE_TAB(bridge, "PaneWebContentsDelegate.openExternalProtocol",
                 base::DictValue().Set("url", url.spec()));
  ScopedAppCallback callback;
  [delegate webContents:tab openExternalProtocol:request];
  return true;
}

}  // namespace

void InstallUIHooks() {
  g_pane_create_login_handler = &LoginHandler::Create;
  g_pane_external_protocol_dialog = &RunExternalProtocolDialog;
  InstallExtensionHooks();
  InstallSelectFileDialogFactory();
}

}  // namespace pane
