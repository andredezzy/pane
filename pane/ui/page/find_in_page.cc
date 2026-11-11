// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/page/find_in_page.h"

#include <algorithm>
#include <utility>

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "components/find_in_page/find_notification_details.h"
#include "components/find_in_page/find_tab_helper.h"
#include "components/find_in_page/find_types.h"
#include "content/public/browser/web_contents.h"
#include "pane/ui/interaction_log.h"
#include "ui/gfx/range/range.h"

namespace pane {

DEFINE_USER_DATA(FindInPage);

FindInPage::FindInPage(base::WeakPtr<Host> host,
                       BrowserWindowInterface& browser)
    : host_(std::move(host)),
      browser_(browser),
      scoped_data_holder_(browser.GetUnownedUserDataHost(), *this) {}

FindInPage::~FindInPage() = default;

// static
FindInPage* FindInPage::From(BrowserWindowInterface* browser) {
  return browser ? Get(browser->GetUnownedUserDataHost()) : nullptr;
}

content::WebContents* FindInPage::page() const {
  return controller_ ? controller_->web_contents() : nullptr;
}

void FindInPage::Changed() {
  if (host_) {
    host_->FindChanged(this);
  }
}

void FindInPage::Open() {
  content::WebContents* contents = page();
  if (!contents) {
    return;
  }
  find_in_page::FindTabHelper::FromWebContents(contents)->set_find_ui_active(
      true);
  Show(/*animate=*/true, /*focus=*/true);
}

// As Chrome's FindBarView::Find, but for the system's find text, which the
// Swift's bar never wrote (OnUserChangedFindText).
void FindInPage::Find(const std::u16string& text) {
  state_.text = text;
  content::WebContents* contents = page();
  auto* helper = contents
                     ? find_in_page::FindTabHelper::FromWebContents(contents)
                     : nullptr;
  if (text.empty()) {
    state_.matches.reset();
    state_.active = 0;
    // Chrome's empty search unselects the page (kClear); the Swift's kept
    // it, and its session goes on.
    if (helper) {
      helper->StopFinding(find_in_page::SelectionAction::kKeep);
      helper->set_find_ui_active(true);
    }
    Changed();
    return;
  }
  if (helper) {
    helper->StartFinding(text, /*forward_direction=*/true,
                         /*case_sensitive=*/false, /*find_match=*/true);
  }
  Changed();
}

void FindInPage::Step(bool forward) {
  content::WebContents* contents = page();
  if (state_.text.empty() || !contents) {
    return;
  }
  if (!step_interval_) {
    step_interval_ = BeginInteractionInterval(Interaction::kFindStep);
  }
  find_in_page::FindTabHelper::FromWebContents(contents)->StartFinding(
      state_.text, forward, /*case_sensitive=*/false, /*find_match=*/true);
}

void FindInPage::Close() {
  if (controller_) {
    controller_->EndFindSession(find_in_page::SelectionAction::kKeep,
                                find_in_page::ResultAction::kClear);
  }
}

FindBarController* FindInPage::GetFindBarController() const {
  return controller_;
}

void FindInPage::SetFindBarController(FindBarController* controller) {
  controller_ = controller;
}

void FindInPage::Show(bool animate, bool focus) {
  state_.visible = true;
  Changed();
  if (focus && host_) {
    host_->FocusFind(this);
  }
}

void FindInPage::Hide(bool animate) {
  state_.visible = false;
  step_interval_.reset();
  Changed();
}

void FindInPage::SetFocusAndSelection() {
  if (state_.visible && host_) {
    host_->FocusFind(this);
  }
}

// The field empties as the bar closes (ResultAction::kClear), as the
// Swift's reset it.
void FindInPage::ClearResults(
    const find_in_page::FindNotificationDetails& results) {
  state_.text.clear();
  state_.matches.reset();
  state_.active = 0;
  Changed();
}

// A text Chrome hands the bar: the system's find text (FindBarPlatformHelper,
// as the bar is made and when Pane comes forward). Taken while the bar shows,
// as Chrome searches it then; not while it's hidden, so the bar opens empty,
// as the Swift's always did.
void FindInPage::SetFindTextAndSelectedRange(const std::u16string& find_text,
                                             const gfx::Range& selected_range) {
  if (!state_.visible || find_text == state_.text) {
    return;
  }
  state_.text = find_text;
  state_.matches.reset();
  state_.active = 0;
  Changed();
}

std::u16string_view FindInPage::GetFindText() const {
  return state_.text;
}

gfx::Range FindInPage::GetSelectedRange() const {
  return gfx::Range();
}

void FindInPage::UpdateUIForFindResult(
    const find_in_page::FindNotificationDetails& result,
    const std::u16string& find_text) {
  // Another text's, a search the field has moved on from.
  if (find_text != state_.text) {
    return;
  }
  if (find_text.empty() || result.number_of_matches() < 0) {
    state_.matches.reset();
    state_.active = 0;
    Changed();
    return;
  }
  state_.matches = result.number_of_matches();
  state_.active = std::max(result.active_match_ordinal(), 0);
  Changed();
  // A step's count shows with the frame after its match is found.
  if (step_interval_ && result.active_match_ordinal() > 0 && host_) {
    EndInteractionAtNextFrame(Interaction::kFindStep,
                              *std::exchange(step_interval_, std::nullopt),
                              host_->FindCompositor());
  }
}

bool FindInPage::IsFindBarVisible() const {
  return state_.visible;
}

// The page has the keys again, as the Swift's closeFind gave it them.
void FindInPage::RestoreSavedFocus() {
  if (host_) {
    host_->FindClosed(this);
  }
}

// One find text for every tab, so none is prefilled from another tab's: the
// Swift's field was the window's, empty at each opening.
bool FindInPage::HasGlobalFindPasteboard() const {
  return true;
}

void FindInPage::UpdateFindBarForChangedWebContents() {
  Changed();
}

// ⌘F never fills the field with the page's selection: the Swift's didn't.
bool FindInPage::CanPopulateFromSelectedText() {
  return false;
}

const FindBarTesting* FindInPage::GetFindBarTesting() const {
  return nullptr;
}

bool FindInPage::HasFocus() const {
  return host_ && host_->FindHasFocus(this);
}

views::Widget* FindInPage::GetHostWidget() {
  return nullptr;
}

}  // namespace pane
