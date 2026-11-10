// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/page_dialogs.h"

#include <utility>

#include "base/auto_reset.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "pane/browser/engine_trace.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/tabs/tab_id.h"
#include "pane/ui/window/root_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace pane {

namespace {

PageDialogs* g_dialogs = nullptr;

// The kind as the Swift's snapshot named it, and as its engine's trace did.
std::string_view SheetKind(PageDialogs::Kind kind) {
  switch (kind) {
    case PageDialogs::Kind::kJavaScript:
      return "javaScript";
    case PageDialogs::Kind::kPermission:
      return "permission";
    case PageDialogs::Kind::kAuthentication:
      return "authentication";
    case PageDialogs::Kind::kExternalProtocol:
      return "externalProtocol";
    case PageDialogs::Kind::kUnresponsive:
      return "unresponsive";
  }
}

std::string_view TraceKind(PageDialogs::Kind kind) {
  return kind == PageDialogs::Kind::kJavaScript ? "javaScriptDialog"
                                                : SheetKind(kind);
}

}  // namespace

std::u16string HostOf(const GURL& url) {
  return base::UTF8ToUTF16(url.has_host() ? url.host() : url.spec());
}

void TraceDialog(std::string_view kind,
                 std::string_view name,
                 content::WebContents* page,
                 base::DictValue args) {
  if (!IsTracing()) {
    return;
  }
  if (page) {
    args.Set("tab", sessions::SessionTabHelper::IdForTab(page).id());
  }
  Trace(kind, name, std::move(args));
}

// One request: its page, watched for going, what its dialog shows, and what
// answers it.
class PageDialogs::Request : public content::WebContentsObserver {
 public:
  Request(PageDialogs* dialogs,
          Id id,
          content::WebContents* page,
          Kind kind,
          PageDialogContent content,
          Answer answer,
          base::OnceClosure page_gone)
      : content::WebContentsObserver(page),
        dialogs_(dialogs),
        id_(id),
        kind_(kind),
        content_(std::move(content)),
        answer_(std::move(answer)),
        page_gone_(std::move(page_gone)) {}

  Id id() const { return id_; }
  Kind kind() const { return kind_; }
  const PageDialogContent& content() const { return content_; }
  content::WebContents* page() const { return web_contents(); }

  void Answer(std::optional<size_t> button,
              const std::vector<std::u16string>& texts) {
    if (answer_) {
      std::move(answer_).Run(button, texts);
    }
  }

  // The engine took it back, or its page went (ADR 0017's withdrawal): the
  // Swift's engine told the app so.
  void TraceDismissed() {
    PANE_TRACE("callback", "PaneWebContentsDelegate.didDismissRequest",
               base::DictValue().Set("kind", TraceKind(kind_)));
  }

  base::OnceClosure TakePageGone() { return std::move(page_gone_); }

  // content::WebContentsObserver:
  void WebContentsDestroyed() override { dialogs_->PageGone(id_); }

 private:
  const raw_ptr<PageDialogs> dialogs_;
  const Id id_;
  const Kind kind_;
  const PageDialogContent content_;
  PageDialogs::Answer answer_;
  base::OnceClosure page_gone_;
};

// static
PageDialogs* PageDialogs::Get() {
  return g_dialogs;
}

PageDialogs::PageDialogs(Delegate* delegate) : delegate_(delegate) {
  CHECK(!g_dialogs);
  g_dialogs = this;
}

PageDialogs::~PageDialogs() {
  g_dialogs = nullptr;
}

PageDialogs::Id PageDialogs::Ask(content::WebContents* page,
                                 Kind kind,
                                 PageDialogContent content,
                                 Answer answer,
                                 base::OnceClosure page_gone) {
  // A page's dialog, or a permission prompt, that shows as it is asked is
  // timed from the page's asking to its frame on the screen.
  RootView* root = delegate_->DialogRoot();
  const bool shows_now = !shown_ && !answering_ && root && page &&
                         page == delegate_->PageForDialogs();
  const std::optional<Interaction> timed =
      !shows_now                  ? std::nullopt
      : kind == Kind::kPermission ? Interaction::kShowPermissionPrompt
      : kind == Kind::kJavaScript ? Interaction::kShowPageDialog
                                  : std::optional<Interaction>();
  const uint64_t interval = timed ? BeginInteractionInterval(*timed) : 0;
  const Id id = next_id_++;
  waiting_.push_back(
      std::make_unique<Request>(this, id, page, kind, std::move(content),
                                std::move(answer), std::move(page_gone)));
  ShowNext();
  if (timed) {
    EndInteractionAtNextFrame(
        *timed, interval,
        root->GetWidget() ? root->GetWidget()->GetCompositor() : nullptr);
  }
  return id;
}

bool PageDialogs::Has(Id id) const {
  if (shown_ && shown_->id() == id) {
    return true;
  }
  for (const std::unique_ptr<Request>& request : waiting_) {
    if (request->id() == id) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<PageDialogs::Request> PageDialogs::Take(Id id) {
  if (shown_ && shown_->id() == id) {
    if (RootView* root = delegate_->DialogRoot()) {
      root->HidePageDialog();
    }
    ShowNextSoon();
    return std::move(shown_);
  }
  for (auto it = waiting_.begin(); it != waiting_.end(); ++it) {
    if ((*it)->id() == id) {
      std::unique_ptr<Request> request = std::move(*it);
      waiting_.erase(it);
      return request;
    }
  }
  return nullptr;
}

void PageDialogs::Withdraw(Id id) {
  if (std::unique_ptr<Request> request = Take(id)) {
    request->TraceDismissed();
  }
}

void PageDialogs::AnswerWithout(Id id,
                                std::optional<size_t> button,
                                const std::vector<std::u16string>& texts) {
  if (std::unique_ptr<Request> request = Take(id)) {
    request->TraceDismissed();
    base::AutoReset<bool> answering(&answering_, true);
    request->Answer(button, texts);
  }
}

void PageDialogs::PageGone(Id id) {
  // Nothing answers a page that went, whose frames are gone.
  if (std::unique_ptr<Request> request = Take(id)) {
    request->TraceDismissed();
    if (base::OnceClosure page_gone = request->TakePageGone()) {
      base::AutoReset<bool> answering(&answering_, true);
      std::move(page_gone).Run();
    }
  }
}

void PageDialogs::Pressed(Id id, std::optional<size_t> button) {
  if (!shown_ || shown_->id() != id) {
    return;
  }
  // Once: a second press in the same frame answers nothing.
  PageDialog* dialog = shown_dialog();
  const std::vector<std::u16string> texts =
      dialog ? dialog->FieldTexts() : std::vector<std::u16string>();
  std::unique_ptr<Request> request = Take(id);
  // What the answer asks next waits for the turn after.
  base::AutoReset<bool> answering(&answering_, true);
  request->Answer(button, texts);
}

void PageDialogs::Update() {
  content::WebContents* page = delegate_->PageForDialogs();
  RootView* root = delegate_->DialogRoot();
  // A dialog of a tab no longer shown, or of a window that closed, goes
  // back in line, first.
  if (shown_ && (shown_->page() != page || !root || !root->page_dialog())) {
    if (root) {
      root->HidePageDialog();
    }
    waiting_.push_front(std::move(shown_));
  }
  // Its page shown again, as its tab is chosen: the dialog keeps the keys,
  // which the page took as it showed.
  if (PageDialog* dialog = shown_ ? root->page_dialog() : nullptr) {
    const views::FocusManager* focus = dialog->GetFocusManager();
    const views::View* focused = focus ? focus->GetFocusedView() : nullptr;
    if (!focused || !dialog->Contains(focused)) {
      dialog->TakeFocus();
    }
    return;
  }
  ShowNext();
}

void PageDialogs::ShowNextSoon() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&PageDialogs::ShowNext, weak_factory_.GetWeakPtr()));
}

void PageDialogs::ShowNext() {
  if (shown_) {
    return;
  }
  if (answering_) {
    ShowNextSoon();
    return;
  }
  content::WebContents* page = delegate_->PageForDialogs();
  RootView* root = delegate_->DialogRoot();
  if (!page || !root) {
    return;
  }
  for (auto it = waiting_.begin(); it != waiting_.end(); ++it) {
    if ((*it)->page() != page) {
      continue;
    }
    shown_ = std::move(*it);
    waiting_.erase(it);
    root->ShowPageDialog(std::make_unique<PageDialog>(
        shown_->content(),
        base::BindRepeating(&PageDialogs::Pressed, weak_factory_.GetWeakPtr(),
                            shown_->id())));
    return;
  }
}

PageDialog* PageDialogs::shown_dialog() {
  RootView* root = shown_ ? delegate_->DialogRoot() : nullptr;
  return root ? root->page_dialog() : nullptr;
}

base::Value PageDialogs::Shown() const {
  if (!shown_) {
    return base::Value();
  }
  const PageDialogContent& content = shown_->content();
  base::ListValue buttons;
  for (const std::u16string& button : content.buttons) {
    buttons.Append(button);
  }
  base::ListValue fields;
  RootView* root = delegate_->DialogRoot();
  PageDialog* dialog = root ? root->page_dialog() : nullptr;
  if (dialog) {
    for (const std::u16string& text : dialog->FieldTexts()) {
      fields.Append(text);
    }
  }
  std::vector<std::u16string> message;
  for (const std::u16string& line : content.message) {
    message.push_back(line);
  }
  return base::Value(
      base::DictValue()
          .Set("kind", SheetKind(shown_->kind()))
          .Set("tabID", shown_->page() ? base::Value(TabIdOf(shown_->page()))
                                       : base::Value())
          .Set("title", content.title)
          .Set("message", base::JoinString(message, u"\n"))
          .Set("buttons", std::move(buttons))
          .Set("fields", std::move(fields)));
}

}  // namespace pane
