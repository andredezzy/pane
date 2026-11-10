// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_PANEL_FRAME_H_
#define PANE_UI_WINDOW_PANEL_FRAME_H_

#include <array>

#include "base/memory/raw_ptr.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace pane {

// The content panel's card on its shadow, in its ring, under everything else
// the window shows (shadow-panel, Elevation.swift). What it paints is four
// bands along the card's edge, each a layer of its own no bigger than the
// edge's shadow and corners, each painting the whole frame clipped to
// itself, so its pixels are the ones a single painting would make; the
// card's middle is a color the compositor draws. The window's own layer then
// holds one color and no tiles: a painted layer is tiled across its bounds
// wherever it draws a path, so the card painted on the window's layer kept
// tiles the size of the window in the GPU process (ADR 0073).
class PanelFrame : public views::View {
  METADATA_HEADER(PanelFrame, views::View)

 public:
  PanelFrame();
  PanelFrame(const PanelFrame&) = delete;
  PanelFrame& operator=(const PanelFrame&) = delete;
  ~PanelFrame() override;

  // The card at `panel`, in this view's coordinates, which are its parent's:
  // it fills its parent. `square` while a page fills the window: the card
  // square and bare.
  void SetPanel(const gfx::Rect& panel, bool square);

  // views::View:
  void Layout(PassKey) override;
  void OnThemeChanged() override;

 private:
  class Band;

  gfx::Rect panel_;
  bool square_ = false;
  std::array<raw_ptr<Band>, 4> bands_;
  raw_ptr<views::View> middle_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_PANEL_FRAME_H_
