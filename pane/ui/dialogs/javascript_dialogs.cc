// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/javascript_dialogs.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace pane {

namespace {

// What the page's dialog accepted with: a prompt's field, or, answered
// without it, the text it was given.
std::u16string TextOf(const std::vector<std::u16string>& texts,
                      const std::u16string& given) {
  return texts.empty() ? given : texts.front();
}

}  // namespace

// static
JavaScriptDialogs* JavaScriptDialogs::Get() {
  static base::NoDestructor<JavaScriptDialogs> dialogs;
  return dialogs.get();
}

JavaScriptDialogs::JavaScriptDialogs() = default;
JavaScriptDialogs::~JavaScriptDialogs() = default;

void JavaScriptDialogs::Ask(content::WebContents* web_contents,
                            std::string_view type,
                            PageDialogContent content,
                            PageDialogs::Answer answer) {
  TraceDialog(
      "callback", "PaneWebContentsDelegate.runJavaScriptDialog", web_contents,
      base::DictValue()
          .Set("type", type)
          .Set("message", base::UTF16ToUTF8(content.message.empty()
                                                ? std::u16string()
                                                : content.message.front())));
  // The tab's one dialog until it is answered, taken back, or its page goes.
  dialogs_[web_contents] = PageDialogs::Get()->Ask(
      web_contents, PageDialogs::Kind::kJavaScript, std::move(content),
      base::BindOnce(
          [](JavaScriptDialogs* self, content::WebContents* web_contents,
             PageDialogs::Answer answer, std::optional<size_t> button,
             const std::vector<std::u16string>& texts) {
            self->dialogs_.erase(web_contents);
            std::move(answer).Run(button, texts);
          },
          base::Unretained(this), web_contents, std::move(answer)),
      base::BindOnce(
          [](JavaScriptDialogs* self, content::WebContents* web_contents) {
            self->dialogs_.erase(web_contents);
          },
          base::Unretained(this), web_contents));
}

void JavaScriptDialogs::RunJavaScriptDialog(
    content::WebContents* web_contents,
    content::RenderFrameHost* render_frame_host,
    content::JavaScriptDialogType dialog_type,
    const std::u16string& message_text,
    const std::u16string& default_prompt_text,
    DialogClosedCallback callback,
    bool* did_suppress_message) {
  // Content answers a suppressed dialog itself, as if cancelled.
  if (!PageDialogs::Get() || dialogs_.contains(web_contents)) {
    *did_suppress_message = true;
    return;
  }
  // "127.0.0.1 says", or a page with no address's "This page says".
  const GURL origin = render_frame_host->GetLastCommittedOrigin().GetURL();
  PageDialogContent content;
  content.title = origin.is_valid() ? base::StrCat({HostOf(origin), u" says"})
                                    : std::u16string(u"This page says");
  content.message = {message_text};
  std::string_view type = "alert";
  switch (dialog_type) {
    case content::JAVASCRIPT_DIALOG_TYPE_ALERT:
      content.buttons = {u"OK"};
      break;
    case content::JAVASCRIPT_DIALOG_TYPE_CONFIRM:
      type = "confirm";
      content.buttons = {u"OK", u"Cancel"};
      break;
    case content::JAVASCRIPT_DIALOG_TYPE_PROMPT:
      type = "prompt";
      content.fields = {{.text = default_prompt_text}};
      content.buttons = {u"OK", u"Cancel"};
      break;
  }
  Ask(web_contents, type, std::move(content),
      base::BindOnce(
          [](DialogClosedCallback callback, std::u16string given,
             std::optional<size_t> button,
             const std::vector<std::u16string>& texts) {
            const bool accepted = button == 0;
            const std::u16string text =
                accepted ? TextOf(texts, given) : std::u16string();
            std::move(callback).Run(accepted, text);
          },
          std::move(callback),
          dialog_type == content::JAVASCRIPT_DIALOG_TYPE_PROMPT
              ? default_prompt_text
              : std::u16string()));
}

void JavaScriptDialogs::RunBeforeUnloadDialog(
    content::WebContents* web_contents,
    content::RenderFrameHost* render_frame_host,
    bool is_reload,
    DialogClosedCallback callback) {
  // Without anyone to ask, the page may go.
  if (!PageDialogs::Get() || dialogs_.contains(web_contents)) {
    std::move(callback).Run(true, std::u16string());
    return;
  }
  PageDialogContent content;
  content.title = is_reload ? u"Reload site?" : u"Leave site?";
  content.message = {u"Changes you made may not be saved."};
  content.buttons = {is_reload ? u"Reload" : u"Leave", u"Cancel"};
  Ask(web_contents, "beforeunload", std::move(content),
      base::BindOnce(
          [](DialogClosedCallback callback, std::optional<size_t> button,
             const std::vector<std::u16string>& texts) {
            std::move(callback).Run(button == 0, std::u16string());
          },
          std::move(callback)));
}

bool JavaScriptDialogs::HandleJavaScriptDialog(
    content::WebContents* web_contents,
    bool accept,
    const std::u16string* prompt_override) {
  const auto found = dialogs_.find(web_contents);
  PageDialogs* dialogs = PageDialogs::Get();
  if (found == dialogs_.end() || !dialogs) {
    return false;
  }
  // A prompt accepted with no text of the protocol's keeps the text it was
  // given.
  std::vector<std::u16string> texts;
  if (prompt_override) {
    texts.push_back(*prompt_override);
  }
  dialogs->AnswerWithout(
      found->second, accept ? std::optional<size_t>(0) : std::nullopt, texts);
  return true;
}

void JavaScriptDialogs::CancelDialogs(content::WebContents* web_contents,
                                      bool reset_state) {
  const auto found = dialogs_.find(web_contents);
  if (found != dialogs_.end() && PageDialogs::Get()) {
    PageDialogs::Get()->AnswerWithout(found->second, std::nullopt, {});
  }
}

void JavaScriptDialogs::RunFormResubmissionDialog(
    content::WebContents* web_contents) {
  if (!PageDialogs::Get() || dialogs_.contains(web_contents)) {
    web_contents->GetController().CancelPendingReload();
    return;
  }
  PageDialogContent content;
  content.title = u"Confirm Form Resubmission";
  content.message = {
      u"This page needs data you entered earlier. Sending it again repeats "
      u"any action the page performed."};
  content.buttons = {u"Continue", u"Cancel"};
  Ask(web_contents, "formResubmission", std::move(content),
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> contents,
             std::optional<size_t> button,
             const std::vector<std::u16string>& texts) {
            if (!contents) {
              return;
            }
            if (button == 0) {
              contents->GetController().ContinuePendingReload();
            } else {
              contents->GetController().CancelPendingReload();
            }
          },
          web_contents->GetWeakPtr()));
}

}  // namespace pane
