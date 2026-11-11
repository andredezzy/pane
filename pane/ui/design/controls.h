// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_CONTROLS_H_
#define PANE_UI_DESIGN_CONTROLS_H_

#include <optional>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/color/color_variant.h"
#include "ui/views/background.h"

namespace gfx {
class Canvas;
class RectF;
}  // namespace gfx

namespace ui {
class ColorProvider;
}

namespace views {
class Button;
class View;
}  // namespace views

namespace pane {

class FocusRing;

// The shape a control's fill, border, and ring take: a squircle of one of
// the scale's radii, or a circle or capsule, which stay round (DESIGN.md ›
// Shape: Capsule(style: .circular)).
class ControlShape {
 public:
  static ControlShape Squircle(float radius);
  static ControlShape Circle();
  static ControlShape Capsule();

  ControlShape(const ControlShape&);
  ControlShape& operator=(const ControlShape&);

  // The shape filling `bounds`.
  SkPath PathIn(const gfx::RectF& bounds) const;
  // Its line `width` wide just inside the edge (SwiftUI's strokeBorder).
  SkPath StrokePathIn(const gfx::RectF& bounds, float width) const;
  // The same kind of shape `outset` larger all round: a squircle's radius
  // grows by as much, as the Swift's 1-point rings take (ADR 0057).
  ControlShape Outset(float outset) const;

  bool is_squircle() const { return kind_ == Kind::kSquircle; }
  float radius() const { return radius_; }

 private:
  enum class Kind { kSquircle, kCircle, kCapsule };

  ControlShape(Kind kind, float radius);

  Kind kind_;
  float radius_;
};

// How a control looks at rest, under the pointer, and pressed: the Swift's
// button styles (Buttons.swift), one painter for all. A selection's fill is
// a look of its own, swapped in at once (DESIGN.md › Motion).
struct ControlLook {
  explicit ControlLook(ControlShape shape);
  ControlLook(const ControlLook&);
  ControlLook& operator=(const ControlLook&);
  ~ControlLook();

  ControlShape shape;
  // The fill at rest, and how opaque it turns under the pointer (the
  // primary's 90%, the secondary's 80%).
  std::optional<ui::ColorVariant> fill;
  double lit_fill_opacity = 1;
  // What fades in over it under the pointer: the outline's 4%, the accent.
  std::optional<ui::ColorVariant> hover_fill;
  // Over the fill while pressed: the foreground at 6%, the card at 10% over
  // a strong fill.
  ui::ColorVariant press = kPaneColorPress;
  // A 1-point border just inside the edge.
  std::optional<ui::ColorVariant> border;
  // The title's or icon's color, and the one it turns under the pointer,
  // with the fill.
  std::optional<ui::ColorVariant> tint;
  std::optional<ui::ColorVariant> lit_tint;
  // The whole control's opacity while disabled: a button's, a field's, and a
  // switch's 50%; an icon button's 40%, Aside's gray.
  float disabled_opacity = 0.5f;
};

// packages/ui's Button variants, as the Swift drew them; a page's dialog's,
// as macOS draws an alert's (DialogButtonStyle): capsules, gray, or blue for
// the default, a press darkening them at once, and no hover; and Aside's
// popups' (the Swift's .popup(isPrimary:)): the primary, and Cancel on the
// secondary fill with the foreground's 8% just inside its edge, each rounded
// 15.
enum class ButtonVariant {
  kPrimary,
  kDestructive,
  kOutline,
  kSecondary,
  kGhost,
  kAlert,
  kAlertDefault,
  kPopupPrimary,
  kPopupCancel,
};

// A button of the variant (PaneButtonStyle), rounded at `radius`: 11, the
// scale's md, unless a popup's or a dialog's 15; an alert's is a capsule.
ControlLook ButtonLook(ButtonVariant variant, float radius = radius::kMd);
// An icon button (IconButtonStyle): the icon muted, darkening as the accent
// fades in under it.
ControlLook IconButtonLook(ControlShape shape = ControlShape::Circle());
// A sidebar row (RowButtonStyle): the accent fading in under the pointer as
// its text goes from muted to the accent's foreground, unless it's the
// selected row, whose fill (the foreground at 5%) and text show at once. New
// keeps its muted text under the pointer.
ControlLook RowLook(bool selected, bool tints_on_hover = true);

// A color `share` of the way from `from` to `to`, by its components.
SkColor Mix(SkColor from, SkColor to, double share);

// A control's states, as the Swift's buttons show them: the hover fades in
// and out with Motion.hover; a press shows at once and fades as it lets go,
// on the same curve; a disabled control shows neither and, as a whole, dims
// to its look's opacity on a layer of its own, so its fill and title don't
// show through each other. Ink drops stay off, and Chromium's ring gives way
// to Pane's, around `shape`. `changed` runs at each step, to paint again (and
// retint).
class ControlHighlight {
 public:
  ControlHighlight(views::Button* button,
                   ControlShape shape,
                   float disabled_opacity,
                   base::RepeatingClosure changed);
  ControlHighlight(const ControlHighlight&) = delete;
  ControlHighlight& operator=(const ControlHighlight&) = delete;
  ~ControlHighlight();

  // How lit and how pressed, 0 to 1.
  double hover() const { return hover_.value(); }
  double press() const { return press_.value(); }
  // Holds the hover's look while what the control opens shows (an
  // extension's popup, a menu), as IconButtonStyle's isOn. Let go, the look
  // follows the pointer again, fading as a hover does if it left.
  void SetHeld(bool held);
  // While a drag carries the control's row (the Swift's isDraggedRow), it
  // shows no hover or press of its own: the drag's copy over it has them.
  // The hover goes at once and a press fades as it lets go; given back, the
  // hover returns on the drop's curve.
  void SetDragged(bool dragged);
  // A press that fades in as well as out, on the hover's curve, as the PIN
  // screen's keys fill (the Swift's DigitKey animated its press both ways).
  void SetPressFadesIn(bool fades_in) { press_fades_in_ = fades_in; }
  // The ring shown while the control has the keyboard.
  FocusRing* ring() { return ring_; }

 private:
  // From the button's state: the hover fades on `hover_curve`, or changes at
  // once without one; a press shows at once and, if `press_fades`, fades as
  // it lets go.
  void Update(std::optional<Curve> hover_curve, bool press_fades);

  const raw_ptr<views::Button> button_;
  raw_ptr<FocusRing> ring_ = nullptr;
  const float disabled_opacity_;
  bool held_ = false;
  bool dragged_ = false;
  bool press_fades_in_ = false;
  // Whether the control is dimmed, and the dimming layer is this one's, to
  // take away once enabled.
  bool dimmed_ = false;
  bool made_layer_ = false;
  ValueMotion hover_;
  ValueMotion press_;
  base::CallbackListSubscription state_subscription_;
};

// Paints a look: its fill, the hover's and the press's over it, and its
// border, in the shape filling `bounds`.
void PaintControl(gfx::Canvas& canvas,
                  const ui::ColorProvider& colors,
                  const ControlLook& look,
                  const gfx::RectF& bounds,
                  double hover,
                  double press);

// A view's background in a look, lit and pressed as `highlight` says, or at
// rest without one.
class ControlBackground : public views::Background {
 public:
  ControlBackground(const ControlLook& look, const ControlHighlight* highlight);
  ~ControlBackground() override;

  void SetLook(const ControlLook& look);
  const ControlLook& look() const { return look_; }

  // views::Background:
  void Paint(gfx::Canvas* canvas, views::View* view) const override;

 private:
  ControlLook look_;
  raw_ptr<const ControlHighlight> highlight_;
};

// The look's tint `hover` of the way to its lit one, resolved by `colors`.
std::optional<SkColor> TintOf(const ControlLook& look,
                              const ui::ColorProvider& colors,
                              double hover);

}  // namespace pane

#endif  // PANE_UI_DESIGN_CONTROLS_H_
