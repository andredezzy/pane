// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/switch.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/draw_looper.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/focus_ring.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/text.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/outsets_f.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

constexpr float kThumb = 16;
constexpr float kThumbInset = 2;
// Between the label and the track.
constexpr int kSpacing = 8;
constexpr float kDisabledOpacity = 0.5f;

// shadow-lg under the thumb: two soft layers, each black at 10%, as SwiftUI
// drew them (its shadow's radius is a Gaussian's sigma, half CSS's blur),
// in the view's own units, as gfx's shadows are.
sk_sp<cc::DrawLooper> ThumbShadow() {
  constexpr SkColor4f kShadow = {0, 0, 0, 0.1f};
  cc::DrawLooperBuilder looper;
  looper.AddUnmodifiedContent();
  looper.AddShadow({0, 9}, 6, kShadow, cc::DrawLooper::kOverrideAlphaFlag);
  looper.AddShadow({0, 3.5f}, 2.5f, kShadow,
                   cc::DrawLooper::kOverrideAlphaFlag);
  return looper.Detach();
}

// How far the thumb's shadow reaches past it: the larger layer's 9 down and
// three sigmas of 6 around that.
constexpr gfx::OutsetsF kShadowReach = gfx::OutsetsF::TLBR(9, 18, 27, 18);

}  // namespace

// The thumb on a layer of its own, as views::ToggleButton's is: its shadow
// reaches past the track and past the row's end, where the switch's own
// painting stops at its bounds, as SwiftUI's shadow did not.
class Switch::Thumb : public views::View {
  METADATA_HEADER(Thumb, views::View)

 public:
  explicit Thumb(const Switch* owner) : owner_(owner) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetIsIgnored(true);
  }

  // Around the track, as far as the shadow reaches from either end.
  void Place() {
    gfx::RectF area = owner_->TrackBounds();
    area.Outset(kShadowReach);
    SetBoundsRect(gfx::ToEnclosingRect(area));
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    gfx::RectF thumb = owner_->ThumbBounds();
    thumb.Offset(-x(), -y());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kPaneColorWindowBackground));
    flags.setLooper(ThumbShadow());
    canvas->DrawPath(ControlShape::Circle().PathIn(thumb), flags);
  }

 private:
  raw_ptr<const Switch> owner_;
};

BEGIN_METADATA(Switch, Thumb)
END_METADATA

Switch::Switch(Toggled toggled, const std::u16string& label)
    : views::Button(
          base::BindRepeating(&Switch::Pressed, base::Unretained(this))),
      toggled_(std::move(toggled)),
      position_(0,
                base::BindRepeating(
                    [](Switch* view, double) {
                      view->SchedulePaint();
                      if (view->thumb_) {
                        view->thumb_->SchedulePaint();
                      }
                    },
                    base::Unretained(this))),
      highlight_(
          this,
          ControlShape::Capsule(),
          kDisabledOpacity,
          base::BindRepeating(&Switch::SchedulePaint, base::Unretained(this))) {
  // The ring goes around the track, as a system switch's does, not the row.
  highlight_.ring()->SetAround(base::BindRepeating([](const views::View* view) {
    return gfx::ToEnclosingRect(
        static_cast<const Switch*>(view)->TrackBounds());
  }));
  if (!label.empty()) {
    std::unique_ptr<views::Label> text =
        MakeLabel(label, text::kRowTitle, kPaneColorForeground);
    text->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label_ = AddChildView(std::move(text));
    label_->SetCanProcessEventsWithinSubtree(false);
  }
  thumb_ = AddChildView(std::make_unique<Thumb>(this));
  GetViewAccessibility().SetRole(ax::mojom::Role::kSwitch);
  if (!label.empty()) {
    GetViewAccessibility().SetName(label);
  }
  UpdateAccessibility();
}

Switch::Switch(Toggled toggled,
               std::unique_ptr<views::View> label,
               const std::u16string& name)
    : Switch(std::move(toggled)) {
  // Under the thumb, whose shadow may reach over it.
  label_ = AddChildViewAt(std::move(label), 0);
  // The whole row is the button: a press on the label is the switch's.
  label_->SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetName(name);
}

Switch::~Switch() = default;

void Switch::SetOn(bool on) {
  if (on_ == on) {
    return;
  }
  on_ = on;
  position_.MoveTo(on ? 1 : 0, motion::Toggle(), /*compositor=*/nullptr);
  UpdateAccessibility();
}

gfx::Size Switch::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  if (!label_) {
    return kTrackSize;
  }
  // At a width given, the label takes what the track leaves, as tall as it
  // needs there.
  if (available_size.width().is_bounded()) {
    const int width = available_size.width().value();
    const int label_width = std::max(width - kSpacing - kTrackSize.width(), 0);
    return gfx::Size(width, std::max(label_->GetHeightForWidth(label_width),
                                     kTrackSize.height()));
  }
  const gfx::Size label = label_->GetPreferredSize(available_size);
  return gfx::Size(label.width() + kSpacing + kTrackSize.width(),
                   std::max(label.height(), kTrackSize.height()));
}

void Switch::ChildPreferredSizeChanged(views::View* child) {
  PreferredSizeChanged();
}

void Switch::Layout(PassKey) {
  if (label_) {
    label_->SetBounds(
        0, 0, std::max(width() - kSpacing - kTrackSize.width(), 0), height());
  }
  thumb_->Place();
}

gfx::RectF Switch::TrackBounds() const {
  // At the row's end, in the middle of its height.
  return gfx::RectF(width() - kTrackSize.width(),
                    (height() - kTrackSize.height()) / 2.0f, kTrackSize.width(),
                    kTrackSize.height());
}

void Switch::PaintButtonContents(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  const gfx::RectF track = TrackBounds();
  const double position = position_.value();
  const ControlShape capsule = ControlShape::Capsule();
  const SkPath track_path = capsule.PathIn(track);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(Mix(colors->GetColor(kPaneColorBorder),
                     colors->GetColor(kPaneColorPrimary), position));
  canvas->DrawPath(track_path, flags);
  // A press over the track, as over a button's fill: the card's 10% over the
  // primary, the foreground's 6% over the border's color.
  if (highlight_.press() > 0) {
    const SkColor press =
        colors->GetColor(on_ ? kPaneColorPressOnStrong : kPaneColorPress);
    flags.setColor(SkColorSetA(
        press, static_cast<U8CPU>(SkColorGetA(press) * highlight_.press())));
    canvas->DrawPath(track_path, flags);
  }
  // The thumb crosses over on its own layer (Thumb).
}

gfx::RectF Switch::ThumbBounds() const {
  const gfx::RectF track = TrackBounds();
  const float travel = kTrackSize.width() - 2 * kThumbInset - kThumb;
  return gfx::RectF(track.x() + kThumbInset + travel * position_.value(),
                    track.y() + kThumbInset, kThumb, kThumb);
}

void Switch::Pressed() {
  on_ = !on_;
  // The user's toggle crosses over, as withAnimation(Motion.toggle) did.
  position_.MoveTo(on_ ? 1 : 0, motion::Toggle(),
                   GetWidget() ? GetWidget()->GetCompositor() : nullptr);
  UpdateAccessibility();
  toggled_.Run(on_);
}

void Switch::UpdateAccessibility() {
  GetViewAccessibility().SetCheckedState(on_ ? ax::mojom::CheckedState::kTrue
                                             : ax::mojom::CheckedState::kFalse);
}

BEGIN_METADATA(Switch)
END_METADATA

}  // namespace pane
