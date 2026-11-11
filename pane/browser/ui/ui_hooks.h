// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_UI_UI_HOOKS_H_
#define PANE_BROWSER_UI_UI_HOOKS_H_

// The hook that routes one of Chrome's own dialogs to Pane's window (patch
// 0004-pane-ui-hooks), where Chromium has no seam for it. Like hooks.h,
// Chrome's file defines it and includes this header (nogncheck); //pane sets
// it at startup (ui_hooks.cc). Unset, or returning "not handled", Chrome
// shows its own UI. Sign-in goes through Chromium's own seam instead
// (pane/ui/dialogs/sign_in.h).

#include <optional>
#include <string>

#include "content/public/browser/weak_document_ptr.h"
#include "url/origin.h"

class GURL;

namespace content {
class WebContents;
}  // namespace content

// external_protocol_dialog.cc: asks before opening another app for a link;
// false keeps Chrome's dialog (pane/ui/dialogs/external_app.h).
extern bool (*g_pane_external_protocol_dialog)(
    const GURL& url,
    content::WebContents* web_contents,
    const std::optional<url::Origin>& initiating_origin,
    content::WeakDocumentPtr initiator_document,
    const std::u16string& program_name);

#endif  // PANE_BROWSER_UI_UI_HOOKS_H_
