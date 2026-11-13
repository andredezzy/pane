// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DIALOGS_PAGE_DIALOGS_H_
#define PANE_UI_DIALOGS_PAGE_DIALOGS_H_

#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "pane/ui/dialogs/page_dialog.h"

class GURL;

namespace content {
class WebContents;
}

namespace pane {

class RootView;

// Where a page is, as its dialogs name it (the Swift's DialogPresenter.host):
// its host, or its whole address where it has none.
std::u16string HostOf(const GURL& url);

// Traces what a page asked (`kind` "callback") or what was answered (`kind`
// "call"), under the names the Swift's engine gave them, which the scenarios
// read, with Chrome's id of the page's tab.
void TraceDialog(std::string_view kind,
                 std::string_view name,
                 content::WebContents* page,
                 base::DictValue args = {});

// The page's dialogs of the app's window (the Swift's DialogPresenter): what
// the hooks of Chrome's pages ask (javascript_dialogs.h, sign_in.h,
// permission_prompt.h, external_app.h, unresponsive_page.h), shown one at a
// time, a tab's only while it is the one selected, as Chrome's tab-modal
// dialogs show; the rest wait in the order they came. Behind the Lock, and
// until the profiles have opened, they wait (ADR 0042). A request goes once
// it is answered, taken back by what asked (Chrome withdrew it), or when its
// page goes; the next shows on the turn after, so one never takes another's
// place within a call (ADR 0017).
class PageDialogs {
 public:
  // What asks, as the channel names it ("javaScript", …) and the trace
  // ("javaScriptDialog", …).
  enum class Kind {
    kJavaScript,
    kPermission,
    kAuthentication,
    kExternalProtocol,
    kUnresponsive,
  };
  using Id = uint64_t;
  // The button pressed, or nullopt for Esc, with the fields' text.
  using Answer =
      base::OnceCallback<void(std::optional<size_t> button,
                              const std::vector<std::u16string>& texts)>;

  class Delegate {
   public:
    virtual ~Delegate() = default;
    // Where they show: null while the window is closed.
    virtual RootView* DialogRoot() = 0;
    // The page whose dialogs may show now: the selected tab's, where the
    // user may act; null for none.
    virtual content::WebContents* PageForDialogs() = 0;
  };

  // The app's, while it runs: the hooks ask through it.
  static PageDialogs* Get();

  explicit PageDialogs(Delegate* delegate);
  PageDialogs(const PageDialogs&) = delete;
  PageDialogs& operator=(const PageDialogs&) = delete;
  ~PageDialogs();

  // Asks for `page`, once it is the page shown. `page_gone` runs when it
  // goes unanswered with its page, which nothing then answers.
  Id Ask(content::WebContents* page,
         Kind kind,
         PageDialogContent content,
         Answer answer,
         base::OnceClosure page_gone = base::OnceClosure());
  // What asked takes it back unanswered: it leaves the window, or the line.
  // Nothing runs.
  void Withdraw(Id id);
  // Answers it as its dialog would, without its dialog: Chrome's DevTools
  // protocol (Page.handleJavaScriptDialog). It leaves the window.
  void AnswerWithout(Id id,
                     std::optional<size_t> button,
                     const std::vector<std::u16string>& texts);
  // The selected tab, the page shown, the Lock, or the window changed: a
  // dialog of a tab no longer shown goes back in line, first, and the next
  // due shows.
  void Update();

  // For the channel: the dialog shown ({ kind, tabID, title, message,
  // buttons, fields }), or none; how many wait; and the one shown.
  base::Value Shown() const;
  int waiting() const { return static_cast<int>(waiting_.size()); }
  PageDialog* shown_dialog();

 private:
  class Request;

  // The dialog of the request shown was pressed.
  void Pressed(Id id, std::optional<size_t> button);
  // The page of a request went.
  void PageGone(Id id);
  // Takes the request out, of the window or the line, unanswered.
  std::unique_ptr<Request> Take(Id id);
  // The next due shows, on the turn after one went.
  void ShowNextSoon();
  void ShowNext();

  const raw_ptr<Delegate> delegate_;
  Id next_id_ = 1;
  // An answer runs: what it asks shows on the turn after, not in its place.
  bool answering_ = false;
  std::unique_ptr<Request> shown_;
  std::list<std::unique_ptr<Request>> waiting_;
  base::WeakPtrFactory<PageDialogs> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_DIALOGS_PAGE_DIALOGS_H_
