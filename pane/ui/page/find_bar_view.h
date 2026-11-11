// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_PAGE_FIND_BAR_VIEW_H_
#define PANE_UI_PAGE_FIND_BAR_VIEW_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "pane/ui/design/dialog.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace views {
class Textfield;
}

namespace pane {

class DigitCellsLabel;
class IconButton;
class SelectionFollowsWindow;

// Find in page's bar (FindBar.swift, ADR 0067, Paper 04.3): Aside's design,
// 357 by 54 points on the popover's fill, rounded 15, its soft shadow (black
// 25%, 40% on dark, 6 down, a Gaussian of 6) and, in dark, a hairline ring
// inside its edge, on a DialogCard (CardLook). The field starts 16 points
// in, at 13, its caret the text's color, with no placeholder (VoiceOver
// still reads "Find in page"); then, once it holds text, the count (12, its
// digits in cells of one width, Paper 10.6; red when nothing matches), and
// the chevrons, 4 apart, and close, each in a 32-point circle, the
// chevrons off while there is nothing to step to. Typing finds as it goes,
// Return goes to the next match and Shift-Return to the previous, and Esc
// closes: the field's keys, as the Swift's onSubmit and onExitCommand.
class FindBarView : public views::View, public views::TextfieldController {
  METADATA_HEADER(FindBarView, views::View)

 public:
  static constexpr int kWidth = 357;
  static constexpr int kHeight = 54;
  // Where it sits: its top 64 points down the window, its right edge 24 in
  // from the window's (the Swift's FindOverlay).
  static constexpr int kTop = 64;
  static constexpr int kRightInset = 24;

  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    // The field's text as typed: a search from the first match.
    base::RepeatingCallback<void(const std::u16string&)> find;
    // Return or a chevron: the next match, or the previous one.
    base::RepeatingCallback<void(bool forward)> step;
    // Esc, close, or a press anywhere else in the window.
    base::RepeatingClosure close;
  };

  // What the bar shows: its text, and what was found for it, once found:
  // how many matches, and which one is the current one, from 1 (Chrome's
  // ordinal).
  struct State {
    bool visible = false;
    std::u16string text;
    std::optional<int> matches;
    int active = 0;
  };

  // The card it sits on (DialogCard's look).
  static DialogCard::Look CardLook();

  explicit FindBarView(Callbacks callbacks);
  FindBarView(const FindBarView&) = delete;
  FindBarView& operator=(const FindBarView&) = delete;
  ~FindBarView() override;

  // Shows `state`. The field takes its text unless it holds it already, so
  // what is typed stays as typed; given another (as the system's find text
  // reaches a bar that shows), it is all selected.
  void SetState(const State& state);
  const State& state() const { return state_; }
  // The field takes the keys, all of its text selected, unless it has them
  // already (an NSTextField made first responder, as the Swift's ⌘F did).
  void FocusField();
  // `text` in the field as typed over what it held, its caret at its end,
  // and found as typing finds it (the channel's stand-in for the keys).
  void Type(const std::u16string& text);
  views::Textfield* field() { return field_; }
  // Its parts, by the names the Swift gave them ("pane.find.field"); the
  // bar itself, "pane.find", is its card's (RootView::FindBarBounds).
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;
  void OnAfterUserAction(views::Textfield* sender) override;
  void OnAfterPaste() override;

 private:
  Callbacks callbacks_;
  State state_;
  // The text last searched, so a change that leaves it as it was (a
  // selection, the caret moved) searches nothing.
  std::u16string searched_;
  raw_ptr<views::Textfield> field_ = nullptr;
  std::unique_ptr<SelectionFollowsWindow> selection_;
  raw_ptr<DigitCellsLabel> count_ = nullptr;
  raw_ptr<IconButton> previous_ = nullptr;
  raw_ptr<IconButton> next_ = nullptr;
  raw_ptr<IconButton> close_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_PAGE_FIND_BAR_VIEW_H_
