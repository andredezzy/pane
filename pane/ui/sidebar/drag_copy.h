// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_DRAG_COPY_H_
#define PANE_UI_SIDEBAR_DRAG_COPY_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "pane/ui/sidebar/sidebar_content.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace pane {

// The copy of a dragged row that rides the pointer (the Swift's dragCopy, ADR
// 0066): a profile's header or a tab's row, its text and icon in the accent's
// foreground (a profile's count stays muted), on the card's fill, rounded as
// the rows are, with a 1-point border inside and a soft shadow, black at 12%,
// 4 points down, a Gaussian of 8 (Paper 03.11). A tab's has no X and doesn't
// dim. It stands on a layer of its own over the rows, so moving it paints
// nothing, and accessibility skips it: the row itself stays in the list.
class DragCopy : public views::View {
  METADATA_HEADER(DragCopy, views::View)

 public:
  // How far past the card its shadow reaches: three of its Gaussian's sigma
  // and its 4 points down. The view stands that much out all round.
  static constexpr int kShadowReach = 28;

  DragCopy();
  DragCopy(const DragCopy&) = delete;
  DragCopy& operator=(const DragCopy&) = delete;
  ~DragCopy() override;

  // What it shows: a profile's header, or a tab's row.
  void SetProfile(const SidebarProfile& profile);
  void SetTab(const SidebarTab& tab);

  // The card's place, in its parent's coordinates, to the fraction of a
  // point the pointer moved it to: its layer carries the fraction.
  void SetCard(const gfx::RectF& card);
  // The card itself, for a read of where it is.
  const views::View* card_view() const;

  // views::View:
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  class Contents;

  raw_ptr<Contents> contents_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_DRAG_COPY_H_
