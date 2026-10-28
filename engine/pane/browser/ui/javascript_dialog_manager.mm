// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/app_callback.h"
#include "pane/browser/ui/javascript_dialog_manager.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/ui/tab_bridge.h"
#include "url/gurl.h"
#include "url/origin.h"

#import "pane/browser/ui/dialogs_internal.h"
#import "pane/public/PaneWebContents.h"

namespace pane {

namespace {

PaneWebContents* TabFor(content::WebContents* web_contents) {
  TabBridge* bridge = TabBridge::FromWebContents(web_contents);
  return bridge ? bridge->tab() : nil;
}

bool AppTakesDialogs(content::WebContents* web_contents) {
  return [TabFor(web_contents).delegate
      respondsToSelector:@selector(webContents:runJavaScriptDialog:)];
}

const char* TypeName(PaneJavaScriptDialogType type) {
  switch (type) {
    case PaneJavaScriptDialogTypeAlert:
      return "alert";
    case PaneJavaScriptDialogTypeConfirm:
      return "confirm";
    case PaneJavaScriptDialogTypePrompt:
      return "prompt";
    case PaneJavaScriptDialogTypeBeforeUnload:
      return "beforeunload";
    case PaneJavaScriptDialogTypeFormResubmission:
      return "formResubmission";
  }
  return "";
}

}  // namespace

// static
JavaScriptDialogManager* JavaScriptDialogManager::GetInstance() {
  static base::NoDestructor<JavaScriptDialogManager> instance;
  return instance.get();
}

JavaScriptDialogManager::JavaScriptDialogManager() = default;
JavaScriptDialogManager::~JavaScriptDialogManager() = default;

void JavaScriptDialogManager::RunJavaScriptDialog(
    content::WebContents* web_contents,
    content::RenderFrameHost* render_frame_host,
    content::JavaScriptDialogType dialog_type,
    const std::u16string& message_text,
    const std::u16string& default_prompt_text,
    DialogClosedCallback callback,
    bool* did_suppress_message) {
  // Content answers a suppressed dialog itself, as if cancelled.
  if (dialogs_.contains(web_contents) || !AppTakesDialogs(web_contents)) {
    *did_suppress_message = true;
    return;
  }
  PaneJavaScriptDialogType type = PaneJavaScriptDialogTypeAlert;
  switch (dialog_type) {
    case content::JAVASCRIPT_DIALOG_TYPE_ALERT:
      type = PaneJavaScriptDialogTypeAlert;
      break;
    case content::JAVASCRIPT_DIALOG_TYPE_CONFIRM:
      type = PaneJavaScriptDialogTypeConfirm;
      break;
    case content::JAVASCRIPT_DIALOG_TYPE_PROMPT:
      type = PaneJavaScriptDialogTypePrompt;
      break;
  }
  Show(web_contents,
       [[PaneJavaScriptDialog alloc]
            initWithType:type
                 message:message_text
       defaultPromptText:default_prompt_text
               originURL:render_frame_host->GetLastCommittedOrigin().GetURL()
                  reload:NO
                     tab:TabFor(web_contents)
              completion:base::BindOnce(
                             [](JavaScriptDialogManager* self,
                                content::WebContents* web_contents,
                                DialogClosedCallback callback, bool accepted,
                                const std::u16string& text) {
                               self->dialogs_.erase(web_contents);
                               std::move(callback).Run(accepted, text);
                             },
                             base::Unretained(this), web_contents,
                             std::move(callback))]);
}

void JavaScriptDialogManager::RunBeforeUnloadDialog(
    content::WebContents* web_contents,
    content::RenderFrameHost* render_frame_host,
    bool is_reload,
    DialogClosedCallback callback) {
  if (dialogs_.contains(web_contents) || !AppTakesDialogs(web_contents)) {
    // Without anyone to ask, the page may go.
    std::move(callback).Run(true, std::u16string());
    return;
  }
  Show(web_contents,
       [[PaneJavaScriptDialog alloc]
            initWithType:PaneJavaScriptDialogTypeBeforeUnload
                 message:std::u16string()
       defaultPromptText:std::u16string()
               originURL:render_frame_host->GetLastCommittedOrigin().GetURL()
                  reload:is_reload
                     tab:TabFor(web_contents)
              completion:base::BindOnce(
                             [](JavaScriptDialogManager* self,
                                content::WebContents* web_contents,
                                DialogClosedCallback callback, bool accepted,
                                const std::u16string& text) {
                               self->dialogs_.erase(web_contents);
                               std::move(callback).Run(accepted, text);
                             },
                             base::Unretained(this), web_contents,
                             std::move(callback))]);
}

bool JavaScriptDialogManager::HandleJavaScriptDialog(
    content::WebContents* web_contents,
    bool accept,
    const std::u16string* prompt_override) {
  auto it = dialogs_.find(web_contents);
  if (it == dialogs_.end()) {
    return false;
  }
  PaneJavaScriptDialog* dialog = it->second;
  [dialog withdrawAccepting:accept
                       text:prompt_override
                                ? *prompt_override
                                : base::SysNSStringToUTF16(
                                      dialog.defaultPromptText)];
  return true;
}

void JavaScriptDialogManager::CancelDialogs(content::WebContents* web_contents,
                                            bool reset_state) {
  auto it = dialogs_.find(web_contents);
  if (it != dialogs_.end()) {
    PaneJavaScriptDialog* dialog = it->second;
    [dialog withdrawAccepting:NO text:std::u16string()];
  }
}

void JavaScriptDialogManager::TabClosed(content::WebContents* web_contents) {
  auto it = dialogs_.find(web_contents);
  if (it == dialogs_.end()) {
    return;
  }
  PaneJavaScriptDialog* dialog = it->second;
  dialogs_.erase(it);
  [dialog withdraw];
}

void JavaScriptDialogManager::RunFormResubmissionDialog(
    content::WebContents* web_contents) {
  if (dialogs_.contains(web_contents) || !AppTakesDialogs(web_contents)) {
    web_contents->GetController().CancelPendingReload();
    return;
  }
  Show(web_contents,
       [[PaneJavaScriptDialog alloc]
            initWithType:PaneJavaScriptDialogTypeFormResubmission
                 message:std::u16string()
       defaultPromptText:std::u16string()
               originURL:web_contents->GetLastCommittedURL()
                  reload:YES
                     tab:TabFor(web_contents)
              completion:base::BindOnce(
                             [](JavaScriptDialogManager* self,
                                base::WeakPtr<content::WebContents> contents,
                                content::WebContents* key, bool accepted,
                                const std::u16string&) {
                               self->dialogs_.erase(key);
                               if (!contents) {
                                 return;
                               }
                               if (accepted) {
                                 contents->GetController()
                                     .ContinuePendingReload();
                               } else {
                                 contents->GetController()
                                     .CancelPendingReload();
                               }
                             },
                             base::Unretained(this),
                             web_contents->GetWeakPtr(), web_contents)]);
}

bool JavaScriptDialogManager::Show(content::WebContents* web_contents,
                                   PaneJavaScriptDialog* dialog) {
  PaneWebContents* tab = TabFor(web_contents);
  id<PaneWebContentsDelegate> delegate = tab.delegate;
  if (![delegate respondsToSelector:@selector(webContents:
                                       runJavaScriptDialog:)]) {
    return false;
  }
  // Recorded first: the app may answer before -runJavaScriptDialog: returns.
  dialogs_[web_contents] = dialog;
  PANE_TRACE_TAB(TabBridge::FromWebContents(web_contents),
                 "PaneWebContentsDelegate.runJavaScriptDialog",
                 base::DictValue()
                     .Set("type", TypeName(dialog.type))
                     .Set("message", base::SysNSStringToUTF8(dialog.message)));
  ScopedAppCallback callback;
  [delegate webContents:tab runJavaScriptDialog:dialog];
  return true;
}

}  // namespace pane
