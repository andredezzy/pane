// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DIALOGS_EXTERNAL_APP_H_
#define PANE_UI_DIALOGS_EXTERNAL_APP_H_

#include <optional>
#include <string>

#include "content/public/browser/weak_document_ptr.h"
#include "url/origin.h"

class GURL;

namespace content {
class WebContents;
}

namespace pane {

// Asks, in the window's own dialog (ADR 0021), before a link opens another
// app: "Open “<app>”?", who asks, and Open <app> or Cancel; Esc cancels.
// Chrome's hook for its own dialog (patch 0004's
// g_pane_external_protocol_dialog) calls it, Chromium having no view
// factory for that dialog; false with no window of Pane's to ask in, which
// keeps Chrome's dialog. Nothing opens unless Open is chosen.
bool AskToOpenExternalApp(const GURL& url,
                          content::WebContents* web_contents,
                          const std::optional<url::Origin>& initiating_origin,
                          content::WeakDocumentPtr initiator_document,
                          const std::u16string& program_name);

}  // namespace pane

#endif  // PANE_UI_DIALOGS_EXTERNAL_APP_H_
