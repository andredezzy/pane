// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/unresponsive_page.h"

#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/no_destructor.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/child_process_id.h"
#include "content/public/common/result_codes.h"
#include "pane/ui/dialogs/page_dialogs.h"

namespace pane {

namespace {

// Each page's dialog asked and not yet answered.
std::map<content::WebContents*, PageDialogs::Id>& Asked() {
  static base::NoDestructor<std::map<content::WebContents*, PageDialogs::Id>>
      asked;
  return *asked;
}

}  // namespace

void AskAboutUnresponsivePage(content::WebContents* web_contents,
                              content::RenderWidgetHost* render_widget_host,
                              base::RepeatingClosure hang_monitor_restarter) {
  PageDialogs* dialogs = PageDialogs::Get();
  // Nobody to ask, or asked already: it waits, as Chrome does until the user
  // decides.
  if (!dialogs || Asked().contains(web_contents)) {
    return;
  }
  PageDialogContent content;
  content.title = u"Page Unresponsive";
  content.message = {
      u"You can wait for it to become responsive, or exit the page."};
  content.buttons = {u"Wait", u"Exit Page"};
  Asked()[web_contents] = dialogs->Ask(
      web_contents, PageDialogs::Kind::kUnresponsive, std::move(content),
      base::BindOnce(
          [](content::WebContents* web_contents,
             content::ChildProcessId process,
             base::RepeatingClosure restart_hang_monitor,
             std::optional<size_t> button,
             const std::vector<std::u16string>& texts) {
            Asked().erase(web_contents);
            if (button != 1) {
              restart_hang_monitor.Run();
              return;
            }
            if (content::RenderProcessHost* host =
                    content::RenderProcessHost::FromID(process)) {
              host->Shutdown(content::RESULT_CODE_HUNG);
            }
          },
          web_contents, render_widget_host->GetProcess()->GetID(),
          std::move(hang_monitor_restarter)),
      base::BindOnce(
          [](content::WebContents* web_contents) {
            Asked().erase(web_contents);
          },
          web_contents));
}

void PageResponsiveAgain(content::WebContents* web_contents) {
  const auto found = Asked().find(web_contents);
  if (found == Asked().end()) {
    return;
  }
  const PageDialogs::Id id = found->second;
  Asked().erase(found);
  if (PageDialogs* dialogs = PageDialogs::Get()) {
    dialogs->Withdraw(id);
  }
}

}  // namespace pane
