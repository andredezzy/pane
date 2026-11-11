// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/panel_frame.h"

#include <algorithm>
#include <memory>

#include "cc/paint/draw_looper.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_solid_color.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace pane {

namespace {

// The panel's ring a point outside its card, rounded a point more, and its
// shadow, 0 2px 8px (a Gaussian of 4).
constexpr float kRingRadius = radius::kPanel + 1;
constexpr gfx::Vector2dF kShadowOffset(0, 2);
constexpr float kShadowSigma = 4;

// How far the bands reach: the shadow ends three sigmas (12 points) out,
// moved two down, so 10 above the card, 14 below it, and 12 to either side,
// which the bands cover with two to spare but below, where 14 keeps the
// bottom band within one 64-pixel tile; and the card's corners take its
// radius along each edge. Beyond them, the window shows only its own color
// and the card's.
constexpr int kAbove = 12;
constexpr int kBelow = 14;
constexpr int kSide = 14;
constexpr int kCorner = static_cast<int>(radius::kPanel);

// The window's fill, on the window, then the card on its shadow, with its
// ring: shadow-panel's 1-pixel ring and soft drop, both outside the card as
// CSS's box-shadows are. A page filling the window has the card square and
// bare.
void PaintFrame(gfx::Canvas* canvas,
                const ui::ColorProvider& colors,
                const gfx::RectF& card,
                bool square,
                bool on_window) {
  if (on_window) {
    canvas->DrawColor(colors.GetColor(kPaneColorWindowBackground));
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(colors.GetColor(kPaneColorCard));
  if (square) {
    canvas->DrawRect(card, flags);
    return;
  }
  cc::DrawLooperBuilder shadow;
  shadow.AddUnmodifiedContent();
  shadow.AddShadow({kShadowOffset.x(), kShadowOffset.y()}, kShadowSigma,
                   SkColor4f::FromColor(colors.GetColor(kPaneColorPanelShadow)),
                   cc::DrawLooper::kOverrideAlphaFlag);
  flags.setLooper(shadow.Detach());
  canvas->DrawPath(SquirclePath(card, radius::kPanel), flags);

  cc::PaintFlags ring;
  ring.setAntiAlias(true);
  ring.setStyle(cc::PaintFlags::kStroke_Style);
  ring.setStrokeWidth(1);
  ring.setColor(colors.GetColor(kPaneColorBorder));
  gfx::RectF outside = card;
  outside.Outset(1);
  canvas->DrawPath(SquircleStrokePath(outside, kRingRadius, 1), ring);
}

}  // namespace

// One band of the frame: the whole frame painted in the frame's coordinates,
// clipped to the band, on a layer of its own, opaque on the window.
class PanelFrame::Band : public views::View {
  METADATA_HEADER(Band, views::View)

 public:
  explicit Band(const PanelFrame* frame) : frame_(frame) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(frame->on_window_);
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    canvas->Translate(-bounds().OffsetFromOrigin());
    PaintFrame(canvas, *colors, gfx::RectF(frame_->panel_), frame_->square_,
               frame_->on_window_);
  }

 private:
  const raw_ptr<const PanelFrame> frame_;
};

BEGIN_METADATA(PanelFrame, Band)
END_METADATA

PanelFrame::PanelFrame(bool on_window) : on_window_(on_window) {
  // Not a place a press or VoiceOver finds: the window's own background
  // under it takes the presses, and moves the window (ADR 0036).
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetIsIgnored(true);
  for (raw_ptr<Band>& band : bands_) {
    band = AddChildView(std::make_unique<Band>(this));
  }
  middle_ = AddChildView(std::make_unique<views::View>());
  middle_->SetPaintToLayer(ui::LAYER_SOLID_COLOR);
  middle_->layer()->SetFillsBoundsOpaquely(true);
}

PanelFrame::~PanelFrame() = default;

void PanelFrame::SetPanel(const gfx::Rect& panel, bool square) {
  if (panel == panel_ && square == square_) {
    return;
  }
  const bool repaint = panel.size() != panel_.size() || square != square_;
  panel_ = panel;
  square_ = square;
  InvalidateLayout();
  // A band that only moves with the card keeps what it painted.
  if (repaint) {
    for (Band* band : bands_) {
      band->SchedulePaint();
    }
  }
}

void PanelFrame::Layout(PassKey) {
  const bool shown = !panel_.IsEmpty();
  for (Band* band : bands_) {
    band->SetVisible(shown && !square_);
  }
  middle_->SetVisible(shown);
  if (!shown) {
    return;
  }
  if (square_) {
    middle_->SetBoundsRect(panel_);
    return;
  }
  const gfx::Rect window = GetLocalBounds();
  const int middle_height = std::max(panel_.height() - 2 * kCorner, 0);
  const auto place = [&](views::View* view, const gfx::Rect& rect) {
    gfx::Rect clipped = rect;
    clipped.Intersect(window);
    view->SetBoundsRect(clipped);
  };
  // The top band and the bottom one take the corners; the sides run between.
  place(bands_[0], gfx::Rect(panel_.x() - kSide, panel_.y() - kAbove,
                             panel_.width() + 2 * kSide, kAbove + kCorner));
  place(bands_[1], gfx::Rect(panel_.x() - kSide, panel_.bottom() - kCorner,
                             panel_.width() + 2 * kSide, kCorner + kBelow));
  place(bands_[2], gfx::Rect(panel_.x() - kSide, panel_.y() + kCorner, kSide,
                             middle_height));
  place(bands_[3],
        gfx::Rect(panel_.right(), panel_.y() + kCorner, kSide, middle_height));
  middle_->SetBoundsRect(gfx::Rect(panel_.x(), panel_.y() + kCorner,
                                   panel_.width(), middle_height));
}

void PanelFrame::OnThemeChanged() {
  views::View::OnThemeChanged();
  middle_->layer()->AsSolidColor()->SetColor(
      SkColor4f::FromColor(GetColorProvider()->GetColor(kPaneColorCard)));
}

BEGIN_METADATA(PanelFrame)
END_METADATA

}  // namespace pane
