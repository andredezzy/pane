// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_PAGE_FIND_IN_PAGE_H_
#define PANE_UI_PAGE_FIND_IN_PAGE_H_

#include <cstdint>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/find_bar/find_bar.h"
#include "pane/ui/page/find_bar_view.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace ui {
class Compositor;
}

namespace pane {

// Find in page for one of a window's Browsers, as Chrome's FindBarController
// drives it: the FindBar its BrowserWindow makes (CreateFindBar), so ⌘F,
// the tab's own find session, and its results are Chrome's, while what
// shows is Pane's bar in the window (FindBarView, ADR 0067), not Chrome's
// child widget. Each Browser's holds its bar's state for the tab it finds
// in, and the window's bar shows the one of the Browser it shows (Host).
// Where the controller would bring the system's find text in, the Swift's
// bar stayed Pane's own: it opens empty, never prefilled from the page's
// selection, and Pane writes nothing to the system's find pasteboard; a
// text the system hands a bar that shows is taken, so field and search
// stay one. Closing empties it, as the Swift's did.
class FindInPage : public FindBar {
 public:
  DECLARE_USER_DATA(FindInPage);

  // The window that shows the bar.
  class Host {
   public:
    // The bar's state changed: the window shows it, if this is its Browser's.
    virtual void FindChanged(FindInPage* find) = 0;
    // The field takes the keys, its text selected.
    virtual void FocusFind(FindInPage* find) = 0;
    // The bar closed: the page takes the keys back.
    virtual void FindClosed(FindInPage* find) = 0;
    // Whether the field has the keys, while this Browser's bar shows.
    virtual bool FindHasFocus(const FindInPage* find) const = 0;
    virtual ::ui::Compositor* FindCompositor() = 0;

   protected:
    virtual ~Host() = default;
  };

  FindInPage(base::WeakPtr<Host> host, BrowserWindowInterface& browser);
  FindInPage(const FindInPage&) = delete;
  FindInPage& operator=(const FindInPage&) = delete;
  ~FindInPage() override;

  // The Browser's, once Chrome's controller has made it.
  static FindInPage* From(BrowserWindowInterface* browser);

  BrowserWindowInterface* browser() { return &browser_.get(); }
  const FindBarView::State& state() const { return state_; }

  // ⌘F: the bar shows over the tab, its field taking the keys, a session of
  // the tab's own begun (the controller's find_ui_active, which brings the
  // bar back with the tab). As Chrome's FindBarController::Show, but for its
  // search of what the field holds: empty, as the bar opens, that search
  // unselects the page (StopFinding's kClear), which the Swift's ⌘F left
  // as it was.
  void Open();
  // From the bar: the field's text, as typed (a search from the first
  // match; emptied, the matches go and the page's selection stays, as the
  // Swift's stopFinding); the next match or the previous one (Return, a
  // chevron), timed to the frame that shows its count; and close (Esc, a
  // press elsewhere).
  void Find(const std::u16string& text);
  void Step(bool forward);
  void Close();

  // FindBar:
  FindBarController* GetFindBarController() const override;
  void SetFindBarController(FindBarController* controller) override;
  void Show(bool animate, bool focus) override;
  void Hide(bool animate) override;
  void SetFocusAndSelection() override;
  void ClearResults(
      const find_in_page::FindNotificationDetails& results) override;
  void StopAnimation() override {}
  void MoveWindowIfNecessary() override {}
  void SetFindTextAndSelectedRange(const std::u16string& find_text,
                                   const gfx::Range& selected_range) override;
  std::u16string_view GetFindText() const override;
  gfx::Range GetSelectedRange() const override;
  void UpdateUIForFindResult(
      const find_in_page::FindNotificationDetails& result,
      const std::u16string& find_text) override;
  void AudibleAlert() override {}
  bool IsFindBarVisible() const override;
  void RestoreSavedFocus() override;
  bool HasGlobalFindPasteboard() const override;
  void UpdateFindBarForChangedWebContents() override;
  bool CanPopulateFromSelectedText() override;
  const FindBarTesting* GetFindBarTesting() const override;
  bool HasFocus() const override;
  void CloseOverlappingBubbles() override {}
  views::Widget* GetHostWidget() override;

 private:
  // The page the controller finds in, while it has one.
  content::WebContents* page() const;
  void Changed();

  const base::WeakPtr<Host> host_;
  const raw_ref<BrowserWindowInterface> browser_;
  raw_ptr<FindBarController> controller_ = nullptr;
  FindBarView::State state_;
  // A step under way, timed until its count shows.
  std::optional<uint64_t> step_interval_;
  ::ui::ScopedUnownedUserData<FindInPage> scoped_data_holder_;
};

}  // namespace pane

#endif  // PANE_UI_PAGE_FIND_IN_PAGE_H_
