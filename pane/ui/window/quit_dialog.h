// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_QUIT_DIALOG_H_
#define PANE_UI_WINDOW_QUIT_DIALOG_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

namespace pane {

class DialogHeading;
class KeyedButton;

// What ⌘Q asks first (ADR 0053; Paper 04.5 Quit Pane, and Quit confirmation
// in 03.5 Overlays; the Swift's QuitConfirmationView), in its dialog over the
// window: a 40-point tile with the dashed circle-x, the title and what
// quitting does, then Cancel (Esc) and the destructive Quit (⌘Q), 16 apart,
// the buttons 8 further down, at the right, 8 apart.
class QuitDialog : public views::View {
  METADATA_HEADER(QuitDialog, views::View)

 public:
  // The dialog's width, its content 24 in.
  static constexpr int kWidth = 480;

  QuitDialog(base::RepeatingClosure cancel, base::RepeatingClosure quit);
  QuitDialog(const QuitDialog&) = delete;
  QuitDialog& operator=(const QuitDialog&) = delete;
  ~QuitDialog() override;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  class Tile;

  raw_ptr<Tile> tile_ = nullptr;
  raw_ptr<DialogHeading> heading_ = nullptr;
  raw_ptr<KeyedButton> cancel_ = nullptr;
  raw_ptr<KeyedButton> quit_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_QUIT_DIALOG_H_
