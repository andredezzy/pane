// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/external_app.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "content/public/browser/web_contents.h"
#include "pane/ui/dialogs/page_dialogs.h"
#include "url/gurl.h"

namespace pane {

bool AskToOpenExternalApp(const GURL& url,
                          content::WebContents* web_contents,
                          const std::optional<url::Origin>& initiating_origin,
                          content::WeakDocumentPtr initiator_document,
                          const std::u16string& program_name) {
  PageDialogs* dialogs = PageDialogs::Get();
  if (!dialogs || !web_contents) {
    return false;
  }
  const GURL origin = initiating_origin ? initiating_origin->GetURL() : GURL();
  PageDialogContent content;
  content.title = base::StrCat({u"Open “", program_name, u"”?"});
  content.message = {
      origin.is_valid()
          ? base::StrCat({HostOf(origin), u" wants to open this application."})
          : std::u16string(u"A website wants to open this application.")};
  content.buttons = {base::StrCat({u"Open ", program_name}), u"Cancel"};
  dialogs->Ask(
      web_contents, PageDialogs::Kind::kExternalProtocol, std::move(content),
      base::BindOnce(
          [](GURL url, base::WeakPtr<content::WebContents> contents,
             content::WeakDocumentPtr initiator_document,
             std::optional<size_t> button,
             const std::vector<std::u16string>& texts) {
            if (button != 0) {
              return;
            }
            TraceDialog("call", "PaneExternalProtocolRequest.open", nullptr);
            if (contents) {
              ExternalProtocolHandler::LaunchUrlWithoutSecurityCheck(
                  url, contents.get(), std::move(initiator_document));
            }
          },
          url, web_contents->GetWeakPtr(), std::move(initiator_document)));
  return true;
}

}  // namespace pane
