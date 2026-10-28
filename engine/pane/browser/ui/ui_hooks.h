// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_UI_UI_HOOKS_H_
#define PANE_BROWSER_UI_UI_HOOKS_H_

// Hooks that route Chrome's own dialogs and bubbles to the Pane app (patch
// 0004-pane-ui-hooks). Like hooks.h, Chrome's files define them and include
// this header (nogncheck); //pane sets them at startup (ui_hooks.mm). Unset,
// or returning "not handled", Chrome shows its own UI. Each one only takes
// over for tabs of Pane's windows.

#include <memory>
#include <optional>
#include <string>

#include "components/permissions/permission_prompt.h"
#include "content/public/browser/login_delegate.h"
#include "content/public/browser/weak_document_ptr.h"
#include "url/origin.h"

class BrowserWindowInterface;
class GURL;
class LoginHandler;

namespace content {
class RenderFrameHost;
class WebContents;
}  // namespace content

namespace permissions {
class ChooserController;
}

namespace net {
class AuthChallengeInfo;
}

// permission_prompt_factory.cc: the prompt for a permission request; nullptr
// keeps Chrome's bubble.
extern std::unique_ptr<permissions::PermissionPrompt> (
    *g_pane_create_permission_prompt)(
    content::WebContents* web_contents,
    permissions::PermissionPrompt::Delegate* delegate);

// login_handler_views.cc: the HTTP or proxy authentication prompt; nullptr
// keeps Chrome's dialog. `callback` is taken only when a handler is returned.
extern std::unique_ptr<LoginHandler> (*g_pane_create_login_handler)(
    const net::AuthChallengeInfo& auth_info,
    content::WebContents* web_contents,
    content::LoginDelegate::LoginAuthRequiredCallback& callback);

// external_protocol_dialog.cc: asks before opening another app for a link;
// false keeps Chrome's dialog.
extern bool (*g_pane_external_protocol_dialog)(
    const GURL& url,
    content::WebContents* web_contents,
    const std::optional<url::Origin>& initiating_origin,
    content::WeakDocumentPtr initiator_document,
    const std::u16string& program_name);

// devtools_window.cc, devtools_window_views.cc: Pane docks DevTools beside
// the tab of a Browser it hosts (Chrome docks them in the BrowserView), and
// is told each time docked DevTools appear, change, or go.
extern bool (*g_pane_hosts_devtools)(BrowserWindowInterface* browser);
extern void (*g_pane_update_devtools)(BrowserWindowInterface* browser,
                                      content::WebContents* inspected);

// print_view_manager_base.cc: a page called window.print().
extern void (*g_pane_will_print)(content::WebContents* web_contents);

// chooser_bubble_ui.cc: a page asks to pick a USB, HID, serial, or Bluetooth
// device. True when Pane answered (it declines, as the Electron app did);
// `controller` is then consumed.
extern bool (*g_pane_device_chooser)(
    content::RenderFrameHost* owner,
    std::unique_ptr<permissions::ChooserController>& controller);

#endif  // PANE_BROWSER_UI_UI_HOOKS_H_
