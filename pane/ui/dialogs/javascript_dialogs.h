// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DIALOGS_JAVASCRIPT_DIALOGS_H_
#define PANE_UI_DIALOGS_JAVASCRIPT_DIALOGS_H_

#include <map>
#include <string>

#include "base/no_destructor.h"
#include "content/public/browser/javascript_dialog_manager.h"
#include "pane/ui/dialogs/page_dialogs.h"

namespace pane {

// A tab's JavaScript dialogs (alert, confirm, prompt, and Leave or Reload
// site) and the form resubmission warning, in the window's own dialog (ADR
// 0021), where Chrome shows its tab-modal ones: Pane's Browsers' delegate
// hands them here (HostedWebContentsDelegate). One per tab at a time: a page
// that asks again while one is up is answered as cancelled, as Chrome does
// for a dialog it suppresses, and so is one with no window to ask in.
class JavaScriptDialogs : public content::JavaScriptDialogManager {
 public:
  static JavaScriptDialogs* Get();

  JavaScriptDialogs(const JavaScriptDialogs&) = delete;
  JavaScriptDialogs& operator=(const JavaScriptDialogs&) = delete;

  // content::JavaScriptDialogManager:
  void RunJavaScriptDialog(content::WebContents* web_contents,
                           content::RenderFrameHost* render_frame_host,
                           content::JavaScriptDialogType dialog_type,
                           const std::u16string& message_text,
                           const std::u16string& default_prompt_text,
                           DialogClosedCallback callback,
                           bool* did_suppress_message) override;
  void RunBeforeUnloadDialog(content::WebContents* web_contents,
                             content::RenderFrameHost* render_frame_host,
                             bool is_reload,
                             DialogClosedCallback callback) override;
  // Chrome's DevTools protocol answers the tab's dialog
  // (Page.handleJavaScriptDialog): it goes from the window.
  bool HandleJavaScriptDialog(content::WebContents* web_contents,
                              bool accept,
                              const std::u16string* prompt_override) override;
  // The page navigated, or Chrome otherwise took the dialog back: it is
  // answered as cancelled, and goes from the window.
  void CancelDialogs(content::WebContents* web_contents,
                     bool reset_state) override;

  // "Confirm Form Resubmission", before reloading a page posted to:
  // Continue posts again, Cancel or Esc keeps the page.
  void RunFormResubmissionDialog(content::WebContents* web_contents);

 private:
  friend class base::NoDestructor<JavaScriptDialogs>;
  JavaScriptDialogs();
  ~JavaScriptDialogs() override;

  // Asks it in the window, as the tab's one dialog.
  void Ask(content::WebContents* web_contents,
           std::string_view type,
           PageDialogContent content,
           PageDialogs::Answer answer);

  // Each tab's dialog asked and not yet answered.
  std::map<content::WebContents*, PageDialogs::Id> dialogs_;
};

}  // namespace pane

#endif  // PANE_UI_DIALOGS_JAVASCRIPT_DIALOGS_H_
