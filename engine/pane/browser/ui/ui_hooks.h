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

namespace net {
class AuthChallengeInfo;
}

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

#endif  // PANE_BROWSER_UI_UI_HOOKS_H_
