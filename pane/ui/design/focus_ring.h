// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_FOCUS_RING_H_
#define PANE_UI_DESIGN_FOCUS_RING_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "pane/ui/design/controls.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

namespace pane {

// The ring around a control while it has the keyboard, as the Swift's field
// draws one: a 1-point line just outside its edge, on its shape grown by the
// point (a field's 12 around its 11, ADR 0057), in the ring's color; outside
// the border, not as it (ADR 0058). views::FocusRing draws only rectangles
// and rounded ones in 158 (focus_ring.cc, IsPathUsable and
// GetRingRoundRect), so this takes its place, as it works: a child of the
// control that layout leaves alone, on a layer of its own to draw past the
// control's edge, taking no events, unseen by accessibility, shown while the
// control has focus.
class FocusRing : public views::View, public views::ViewObserver {
  METADATA_HEADER(FocusRing, views::View)

 public:
  // Whether the ring shows on the control, past its focus: a field with a
  // problem shows the problem's red border alone.
  using ShowsWhen = base::RepeatingCallback<bool(const views::View* control)>;
  // What of the control the ring goes around, in its bounds: a switch's
  // track, rather than its row.
  using Around = base::RepeatingCallback<gfx::Rect(const views::View* control)>;

  // Adds the ring to `control`, whose own shape is `shape`.
  static FocusRing* Install(views::View* control, ControlShape shape);

  FocusRing(const FocusRing&) = delete;
  FocusRing& operator=(const FocusRing&) = delete;
  ~FocusRing() override;

  void SetShape(ControlShape shape);
  void SetShowsWhen(ShowsWhen shows_when);
  void SetAround(Around around);
  // Shows while a view inside the control has the keys, the control having
  // none of its own (a field of parts: the profile sheet's proxy address),
  // which tells it of each change (Refresh).
  void SetFocusWithin(bool within);
  // Shows or hides it again, after what ShowsWhen reads changed.
  void Refresh();

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

  // views::ViewObserver:
  void OnViewFocused(views::View* view) override;
  void OnViewBlurred(views::View* view) override;
  void OnViewBoundsChanged(views::View* view) override;
  void OnViewAddedToWidget(views::View* view) override;

 private:
  // The ring's width, outside the control's edge.
  static constexpr float kWidth = 1;

  FocusRing(views::View* control, ControlShape shape);

  // Around what it goes, `kWidth` larger all round.
  void UpdateBounds();

  raw_ptr<views::View> control_;
  ControlShape shape_;
  ShowsWhen shows_when_;
  Around around_;
  bool within_ = false;
  base::ScopedObservation<views::View, views::ViewObserver> observation_{this};
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_FOCUS_RING_H_
