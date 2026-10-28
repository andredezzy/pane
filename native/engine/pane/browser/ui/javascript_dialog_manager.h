// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_UI_JAVASCRIPT_DIALOG_MANAGER_H_
#define PANE_BROWSER_UI_JAVASCRIPT_DIALOG_MANAGER_H_

#include <map>
#include <string>

#include "base/no_destructor.h"
#include "content/public/browser/javascript_dialog_manager.h"

@class PaneJavaScriptDialog;

namespace pane {

// Shows a tab's JavaScript dialogs (alert, confirm, prompt, beforeunload) and
// the form resubmission warning through the app, instead of Chrome's
// tab-modal Views dialogs. One dialog per tab at a time: a page that asks
// again while one is up is answered "cancelled", as Chrome does for a
// dialog it suppresses.
class JavaScriptDialogManager : public content::JavaScriptDialogManager {
 public:
  static JavaScriptDialogManager* GetInstance();

  JavaScriptDialogManager(const JavaScriptDialogManager&) = delete;
  JavaScriptDialogManager& operator=(const JavaScriptDialogManager&) = delete;

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
  bool HandleJavaScriptDialog(content::WebContents* web_contents,
                              bool accept,
                              const std::u16string* prompt_override) override;
  void CancelDialogs(content::WebContents* web_contents,
                     bool reset_state) override;

  // "Confirm form resubmission" when reloading a page posted to.
  void RunFormResubmissionDialog(content::WebContents* web_contents);

  // The tab is being destroyed: its dialog, if any, is withdrawn without an
  // answer, since its frames are gone. Content's own cancel at destruction
  // never reaches this manager: a closing tab has no delegate by then.
  void TabClosed(content::WebContents* web_contents);

 private:
  friend class base::NoDestructor<JavaScriptDialogManager>;
  JavaScriptDialogManager();
  ~JavaScriptDialogManager() override;

  // Hands `dialog` to the tab's delegate; false when the app takes none.
  bool Show(content::WebContents* web_contents, PaneJavaScriptDialog* dialog);

  std::map<content::WebContents*, PaneJavaScriptDialog* __strong> dialogs_;
};

}  // namespace pane

#endif  // PANE_BROWSER_UI_JAVASCRIPT_DIALOG_MANAGER_H_
