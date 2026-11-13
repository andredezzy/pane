// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_SCROLL_FADE_H_
#define PANE_UI_DESIGN_SCROLL_FADE_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "ui/color/color_variant.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

namespace views {
class ScrollView;
}

namespace pane {

// A vertical scroll view whose edges fade as it scrolls
// (FadingScrollView.swift): the top fade grows from nothing to its full 40
// points over the first 40 scrolled, the bottom one shrinks to nothing over
// the last 40, so nothing fades at either end, or when the content fits. For
// an edge with nothing visible to mark it (DESIGN.md › Scroll edges): an edge
// that a rule, a header, or a footer bounds ends hard, and never takes both.
// The fades are two overlays in the color of what is behind the list, at
// 1 − share² of it from the edge in (the Swift's mask, inverted, at its five
// stops), not a mask, which would make a render surface of the list.
class FadingScrollView : public views::View, public views::ViewObserver {
  METADATA_HEADER(FadingScrollView, views::View)

 public:
  // How far an edge fades, at most (ADR 0061).
  static constexpr int kFade = 40;

  // `behind`: what the list sits on, the overlays' color.
  FadingScrollView(ui::ColorVariant behind, bool fades_top, bool fades_bottom);
  FadingScrollView(const FadingScrollView&) = delete;
  FadingScrollView& operator=(const FadingScrollView&) = delete;
  ~FadingScrollView() override;

  views::ScrollView* scroll_view() { return scroll_view_; }

  // views::View:
  void Layout(PassKey) override;

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* view) override;

 private:
  class Fade;

  // Each fade's height from where the list stands.
  void UpdateFades();
  // The list moved: its fades follow, and what the pointer is over now hears
  // so once the scroll is through.
  void Scrolled();
  // The rows moved under a pointer that didn't: the window takes a move
  // where it is (the Mac sends none, where Aura does), unless a press is
  // under way, which keeps its own.
  void RefreshHover();

  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<Fade> top_ = nullptr;
  raw_ptr<Fade> bottom_ = nullptr;
  base::CallbackListSubscription scrolled_;
  base::ScopedObservation<views::View, views::ViewObserver> contents_{this};
  bool hover_refresh_pending_ = false;
  base::WeakPtrFactory<FadingScrollView> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_SCROLL_FADE_H_
