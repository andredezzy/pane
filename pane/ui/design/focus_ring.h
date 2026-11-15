// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_FOCUS_RING_H_
#define PANE_UI_DESIGN_FOCUS_RING_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "pane/ui/design/controls.h"
#include "ui/color/color_id.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

namespace pane {

// How the system draws the ring of a control the keys reach
// (focus_ring_{mac,win}.cc): its width, just outside the control's edge, its
// color, and how far off the edge of a strong fill it stands.
struct FocusRingStyle {
  float width;
  ui::ColorId color;
  float off_fill;
};

// The Mac's ring, the Swift's: 1 point in the ring's grey. Windows' (Paper
// 11.17 B): 2 points in the text's color, as Windows 11 draws its own focus,
// 1 point off a strong fill, the card between, where a ring in the fill's own
// color would vanish (11.18); Highlight under a contrast theme.
FocusRingStyle ControlFocusRingStyle();

// The ring around a control while it has the keyboard, as the Swift's field
// draws one: a line just outside its edge, on its shape grown by the line
// (a field's 12 around its 11, ADR 0057); outside the border, not as it (ADR
// 0058). A field's is the Mac's 1 point in the ring's color on every system,
// its caret showing where the keys are (Paper 11.17); a control's, the
// system's (ControlFocusRingStyle). views::FocusRing draws only rectangles
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

  // What the ring is on: a control the keys reach (a button, a row, a
  // swatch), or a field.
  enum class Kind { kControl, kField };

  // Adds the ring to `control`, whose own shape is `shape`.
  static FocusRing* Install(views::View* control,
                            ControlShape shape,
                            Kind kind = Kind::kControl);

  FocusRing(const FocusRing&) = delete;
  FocusRing& operator=(const FocusRing&) = delete;
  ~FocusRing() override;

  void SetShowsWhen(ShowsWhen shows_when);
  void SetAround(Around around);
  // The control has a strong fill (a dialog's default or destructive
  // button): the ring stands off it as the system has it.
  void SetOnFill(bool on_fill);
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
  FocusRing(views::View* control, ControlShape shape, Kind kind);

  // How far out from the control's edge the line starts.
  float Gap() const;
  // Around what it goes, as much larger all round as the gap and the line.
  void UpdateBounds();

  raw_ptr<views::View> control_;
  const ControlShape shape_;
  const FocusRingStyle style_;
  bool on_fill_ = false;
  ShowsWhen shows_when_;
  Around around_;
  bool within_ = false;
  base::ScopedObservation<views::View, views::ViewObserver> observation_{this};
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_FOCUS_RING_H_
