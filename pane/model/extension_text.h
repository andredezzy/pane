// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_EXTENSION_TEXT_H_
#define PANE_MODEL_EXTENSION_TEXT_H_

#include <string>
#include <string_view>

#include "url/gurl.h"

namespace pane::model {

// What the popups that add or remove an extension say. Adding names Pane, as
// Dia's dialog names Dia; Chrome's other prompts (new permissions,
// re-enabling) keep their titles, their quotes curled as Remove's are.
std::string ExtensionPromptTitle(std::string_view accept_title,
                                 std::string_view extension_name,
                                 std::string_view chrome_title);
std::string ExtensionPromptAcceptTitle(std::string_view chrome_accept_title);
std::string ExtensionRemoveTitle(std::string_view extension_name);

// Chrome's prompt titles quote the name straight (Add "Dark Reader"?): the
// first and the last straight quote turn curly.
std::string CurlyQuoted(std::string_view title);

// Report abuse, ticked as the extension goes: the Web Store's report page, as
// Chrome's dialog opens it.
GURL ReportAbuseURL(std::string_view extension_id);

}  // namespace pane::model

#endif  // PANE_MODEL_EXTENSION_TEXT_H_
