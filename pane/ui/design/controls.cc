// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/controls.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/focus_ring.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// `color` with its alpha scaled by `share`.
SkColor Faded(SkColor color, double share) {
  return SkColorSetA(
      color, static_cast<U8CPU>(std::lround(SkColorGetA(color) *
                                            std::clamp(share, 0.0, 1.0))));
}

ui::Compositor* CompositorOf(views::View* view) {
  views::Widget* widget = view->GetWidget();
  return widget ? widget->GetCompositor() : nullptr;
}

}  // namespace

// static
ControlShape ControlShape::Squircle(float radius) {
  return ControlShape(Kind::kSquircle, radius);
}

// static
ControlShape ControlShape::Circle() {
  return ControlShape(Kind::kCircle, 0);
}

// static
ControlShape ControlShape::Capsule() {
  return ControlShape(Kind::kCapsule, 0);
}

ControlShape::ControlShape(Kind kind, float radius)
    : kind_(kind), radius_(radius) {}

ControlShape::ControlShape(const ControlShape&) = default;
ControlShape& ControlShape::operator=(const ControlShape&) = default;

SkPath ControlShape::PathIn(const gfx::RectF& bounds) const {
  switch (kind_) {
    case Kind::kSquircle:
      return SquirclePath(bounds, radius_);
    case Kind::kCircle: {
      // A circle in the middle of its frame, as SwiftUI's.
      const float side = std::min(bounds.width(), bounds.height());
      const gfx::PointF center = bounds.CenterPoint();
      return SkPath::Circle(center.x(), center.y(), side / 2);
    }
    case Kind::kCapsule: {
      const float r = std::min(bounds.width(), bounds.height()) / 2;
      return SkPath::RRect(SkRect::MakeXYWH(bounds.x(), bounds.y(),
                                            bounds.width(), bounds.height()),
                           r, r);
    }
  }
}

SkPath ControlShape::StrokePathIn(const gfx::RectF& bounds, float width) const {
  if (kind_ == Kind::kSquircle) {
    return SquircleStrokePath(bounds, radius_, width);
  }
  gfx::RectF inner = bounds;
  inner.Inset(width / 2);
  return PathIn(inner);
}

ControlShape ControlShape::Outset(float outset) const {
  return ControlShape(kind_, kind_ == Kind::kSquircle ? radius_ + outset : 0);
}

ControlLook::ControlLook(ControlShape shape) : shape(shape) {}
ControlLook::ControlLook(const ControlLook&) = default;
ControlLook& ControlLook::operator=(const ControlLook&) = default;
ControlLook::~ControlLook() = default;

ControlLook ButtonLook(ButtonVariant variant, float radius) {
  // A popup's are the primary and the secondary at its own 15.
  if (variant == ButtonVariant::kPopupPrimary ||
      variant == ButtonVariant::kPopupCancel) {
    const bool primary = variant == ButtonVariant::kPopupPrimary;
    ControlLook look = ButtonLook(
        primary ? ButtonVariant::kPrimary : ButtonVariant::kSecondary,
        radius::kLg);
    if (!primary) {
      look.border = kPaneColorPopupBorder;
    }
    return look;
  }
  ControlLook look(ControlShape::Squircle(radius));
  switch (variant) {
    case ButtonVariant::kPrimary:
      look.fill = kPaneColorPrimary;
      look.lit_fill_opacity = 0.9;
      look.press = kPaneColorPressOnStrong;
      look.tint = kPaneColorPrimaryForeground;
      break;
    case ButtonVariant::kDestructive:
      look.fill = kPaneColorDestructive;
      look.lit_fill_opacity = 0.9;
      look.press = kPaneColorPressOnStrong;
      look.tint = kPaneColorDestructiveForeground;
      break;
    case ButtonVariant::kOutline:
      // No fill but its hover's (ADR 0063): the border over whatever it sits
      // on.
      look.hover_fill = kPaneColorOutlineHover;
      look.border = kPaneColorBorder;
      look.tint = kPaneColorForeground;
      look.lit_tint = kPaneColorAccentForeground;
      break;
    case ButtonVariant::kSecondary:
      look.fill = kPaneColorSecondary;
      look.lit_fill_opacity = 0.8;
      look.tint = kPaneColorForeground;
      break;
    case ButtonVariant::kGhost:
      look.hover_fill = kPaneColorAccent;
      look.tint = kPaneColorForeground;
      look.lit_tint = kPaneColorAccentForeground;
      break;
    case ButtonVariant::kPopupPrimary:
    case ButtonVariant::kPopupCancel:
      break;
    case ButtonVariant::kAlert:
    case ButtonVariant::kAlertDefault: {
      const bool default_button = variant == ButtonVariant::kAlertDefault;
      look.shape = ControlShape::Capsule();
      look.fill =
          default_button ? kPaneColorAlertDefaultButton : kPaneColorAlertButton;
      look.press = kPaneColorAlertPress;
      look.tint =
          default_button ? kPaneColorAlertDefaultText : kPaneColorAlertText;
      break;
    }
  }
  return look;
}

ControlLook IconButtonLook(ControlShape shape) {
  ControlLook look(shape);
  look.hover_fill = kPaneColorAccent;
  look.tint = kPaneColorMutedForeground;
  look.lit_tint = kPaneColorAccentForeground;
  look.disabled_opacity = 0.4f;
  return look;
}

ControlLook RowLook(bool selected, bool tints_on_hover) {
  ControlLook look(ControlShape::Squircle(radius::kLg));
  // A row has no disabled look of its own.
  look.disabled_opacity = 1;
  if (selected) {
    look.fill = kPaneColorRowSelected;
    look.tint = kPaneColorAccentForeground;
    return look;
  }
  look.hover_fill = kPaneColorAccent;
  look.tint = kPaneColorMutedForeground;
  if (tints_on_hover) {
    look.lit_tint = kPaneColorAccentForeground;
  }
  return look;
}

SkColor Mix(SkColor from, SkColor to, double share) {
  const auto channel = [&](U8CPU a, U8CPU b) {
    return static_cast<U8CPU>(std::lround(a + (static_cast<double>(b) - a) *
                                                  std::clamp(share, 0.0, 1.0)));
  };
  return SkColorSetARGB(channel(SkColorGetA(from), SkColorGetA(to)),
                        channel(SkColorGetR(from), SkColorGetR(to)),
                        channel(SkColorGetG(from), SkColorGetG(to)),
                        channel(SkColorGetB(from), SkColorGetB(to)));
}

ControlHighlight::ControlHighlight(views::Button* button,
                                   ControlShape shape,
                                   float disabled_opacity,
                                   base::RepeatingClosure changed)
    : button_(button),
      disabled_opacity_(disabled_opacity),
      hover_(0, base::IgnoreArgs<double>(changed)),
      press_(0, base::IgnoreArgs<double>(changed)) {
  // No ripple, and none of Button's own hover animation, which steps on a
  // timer of its own: the hover is this one's, on the compositor's frames.
  views::InkDrop::Get(button)->SetMode(views::InkDropHost::InkDropMode::OFF);
  button->SetAnimateOnStateChange(false);
  button->SetInstallFocusRingOnFocus(false);
  ring_ = FocusRing::Install(button, shape);
  state_subscription_ = button->AddStateChangedCallback(base::BindRepeating(
      &ControlHighlight::Update, base::Unretained(this),
      std::optional<Curve>(motion::Hover()), /*press_fades=*/true));
  Update(std::nullopt, /*press_fades=*/false);
}

ControlHighlight::~ControlHighlight() = default;

void ControlHighlight::SetHeld(bool held) {
  if (held_ != held) {
    held_ = held;
    // What the control opened shows: lit at once. Gone, the pointer's place
    // decides, on the hover's curve.
    Update(held ? std::nullopt : std::optional<Curve>(motion::Hover()),
           /*press_fades=*/false);
  }
}

void ControlHighlight::SetDragged(bool dragged) {
  if (dragged_ != dragged) {
    dragged_ = dragged;
    // Taken, the hover goes at once as the press fades (PressLayer's);
    // given back with the drop, the hover returns on its curve.
    Update(dragged ? std::nullopt : std::optional<Curve>(motion::Drop()),
           /*press_fades=*/true);
  }
}

void ControlHighlight::Update(std::optional<Curve> hover_curve,
                              bool press_fades) {
  const views::Button::ButtonState state = button_->GetState();
  const bool enabled = state != views::Button::STATE_DISABLED;

  // Disabled, the control dims as one, at once; enabled again, it undoes
  // only its own dimming, and leaves a layer another gave it (a dragged
  // row's) as it is.
  if (!enabled && disabled_opacity_ < 1) {
    if (!button_->layer()) {
      button_->SetPaintToLayer();
      button_->layer()->SetFillsBoundsOpaquely(false);
      made_layer_ = true;
    }
    button_->layer()->SetOpacity(disabled_opacity_);
    dimmed_ = true;
  } else if (std::exchange(dimmed_, false) && button_->layer()) {
    if (std::exchange(made_layer_, false)) {
      button_->DestroyLayer();
    } else {
      button_->layer()->SetOpacity(1);
    }
  }

  // Only the pointer's coming and going fades: disabled, the hover goes at
  // once. A row a drag carries shows neither.
  ui::Compositor* compositor = enabled ? CompositorOf(button_) : nullptr;
  const bool lit = enabled && !dragged_ &&
                   (held_ || state == views::Button::STATE_HOVERED ||
                    state == views::Button::STATE_PRESSED);
  const double hover = lit ? 1 : 0;
  if (hover != hover_.target()) {
    hover_.MoveTo(hover, hover_curve.value_or(motion::Hover()),
                  hover_curve ? compositor : nullptr);
  }
  // A press shows at once, and fades as it lets go.
  const double press =
      enabled && !dragged_ && state == views::Button::STATE_PRESSED ? 1 : 0;
  if (press != press_.target()) {
    press_.MoveTo(press, motion::Hover(),
                  press || !press_fades ? nullptr : compositor);
  }
}

void PaintControl(gfx::Canvas& canvas,
                  const ui::ColorProvider& colors,
                  const ControlLook& look,
                  const gfx::RectF& bounds,
                  double hover,
                  double press) {
  const SkPath path = look.shape.PathIn(bounds);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  if (look.fill) {
    flags.setColor(Faded(look.fill->ResolveToSkColor(&colors),
                         1 - (1 - look.lit_fill_opacity) * hover));
    canvas.DrawPath(path, flags);
  }
  if (look.hover_fill && hover > 0) {
    flags.setColor(Faded(look.hover_fill->ResolveToSkColor(&colors), hover));
    canvas.DrawPath(path, flags);
  }
  if (press > 0) {
    flags.setColor(Faded(look.press.ResolveToSkColor(&colors), press));
    canvas.DrawPath(path, flags);
  }
  if (look.border) {
    flags.setColor(look.border->ResolveToSkColor(&colors));
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    canvas.DrawPath(look.shape.StrokePathIn(bounds, 1), flags);
  }
}

ControlBackground::ControlBackground(const ControlLook& look,
                                     const ControlHighlight* highlight)
    : look_(look), highlight_(highlight) {}

ControlBackground::~ControlBackground() = default;

void ControlBackground::SetLook(const ControlLook& look) {
  look_ = look;
}

void ControlBackground::Paint(gfx::Canvas* canvas, views::View* view) const {
  const ui::ColorProvider* colors = view->GetColorProvider();
  if (!colors) {
    return;
  }
  PaintControl(*canvas, *colors, look_, gfx::RectF(view->GetLocalBounds()),
               highlight_ ? highlight_->hover() : 0,
               highlight_ ? highlight_->press() : 0);
}

std::optional<SkColor> TintOf(const ControlLook& look,
                              const ui::ColorProvider& colors,
                              double hover) {
  if (!look.tint) {
    return std::nullopt;
  }
  const SkColor rest = look.tint->ResolveToSkColor(&colors);
  if (!look.lit_tint || hover <= 0) {
    return rest;
  }
  return Mix(rest, look.lit_tint->ResolveToSkColor(&colors), hover);
}

}  // namespace pane
