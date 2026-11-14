// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/scroll.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/palette.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/scrollbar/base_scroll_bar_thumb.h"
#include "ui/views/controls/scrollbar/scroll_bar.h"

namespace pane {

namespace {

// Windows 11's own (Paper 11.9's A, as its Settings and other apps scroll):
// over the content, taking nothing from it. At rest a 2-point line, 4 in
// from the edge; under the pointer a 6-point thumb on a faint 14-point
// track, with an arrow at each end; gone once nothing moves.
constexpr int kTrack = 14;
constexpr int kInset = 4;
constexpr int kRestThumb = 2;
constexpr int kHoverThumb = 6;
// The arrows: 6 by 5, 6 from each end, 4 in.
constexpr int kArrowWidth = 6;
constexpr int kArrowHeight = 5;
constexpr int kArrowEnd = 6;
// The thumb travels between the arrows' boxes.
constexpr int kArrowBox = kTrack;
// Paper 11.9's colors, black on Pane's light surfaces and white on its dark
// ones: the line at 45%, the thumb and the arrows at 55%; the track #F9F9F9
// or #282828 at 90%, edged on its inner side with 6%.
constexpr SkAlpha kRestAlpha = 0x73;
constexpr SkAlpha kHoverAlpha = 0x8C;
constexpr SkColor kTrackLight = SkColorSetARGB(0xE6, 0xF9, 0xF9, 0xF9);
constexpr SkColor kTrackDark = SkColorSetARGB(0xE6, 0x28, 0x28, 0x28);
constexpr SkAlpha kEdgeAlpha = 0x0F;
// How long it stays once nothing moves, and its fade, as Views' overlay
// scroll bar's.
constexpr base::TimeDelta kHideDelay = base::Seconds(1);
constexpr base::TimeDelta kFade = base::Milliseconds(200);

class FluentScrollBar : public views::ScrollBar {
  METADATA_HEADER(FluentScrollBar, views::ScrollBar)

 public:
  explicit FluentScrollBar(Orientation orientation)
      : views::ScrollBar(orientation) {
    SetNotifyEnterExitOnChild(true);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetOpacity(0);
    thumb_ = new Thumb(this);
    SetThumb(thumb_);
  }

  // Whether the pointer is over it: the track, the arrows, the full thumb.
  bool hovered() const { return hovered_; }

  // The ink for its parts: black on Pane's light surfaces, white on its
  // dark ones.
  SkColor Ink() const {
    const ui::ColorProvider* colors = GetColorProvider();
    return colors && color_utils::IsDark(colors->GetColor(kPaneColorCard))
               ? SK_ColorWHITE
               : SK_ColorBLACK;
  }

  bool IsHorizontal() const {
    return GetOrientation() == Orientation::kHorizontal;
  }

  // views::ScrollBar:
  bool OverlapsContent() const override { return true; }
  int GetThickness() const override { return kTrack; }

  gfx::Rect GetTrackBounds() const override {
    gfx::Rect track = GetLocalBounds();
    if (IsHorizontal()) {
      track.Inset(gfx::Insets::VH(0, kArrowBox));
    } else {
      track.Inset(gfx::Insets::VH(kArrowBox, 0));
    }
    return track;
  }

  void Update(int viewport_size,
              int content_size,
              int contents_scroll_offset) override {
    views::ScrollBar::Update(viewport_size, content_size,
                             contents_scroll_offset);
    if (contents_scroll_offset != offset_) {
      offset_ = contents_scroll_offset;
      Show();
      StartHideCountdown();
    }
  }

  void OnThemeChanged() override {
    views::ScrollBar::OnThemeChanged();
    SchedulePaintAll();
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    hovered_ = true;
    Show();
    SchedulePaintAll();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    hovered_ = false;
    SchedulePaintAll();
    StartHideCountdown();
  }

  // An arrow scrolls a line; the track, as Views' does, a page.
  bool OnMousePressed(const ui::MouseEvent& event) override {
    const int along = IsHorizontal() ? event.x() : event.y();
    const int length = IsHorizontal() ? width() : height();
    if (along < kArrowBox) {
      ScrollByAmount(ScrollAmount::kPrevLine);
      return true;
    }
    if (along >= length - kArrowBox) {
      ScrollByAmount(ScrollAmount::kNextLine);
      return true;
    }
    return views::ScrollBar::OnMousePressed(event);
  }

  // The thumb across the whole bar; ScrollBar::Update places it along it.
  void Layout(PassKey) override {
    gfx::Rect thumb = thumb_->bounds();
    if (IsHorizontal()) {
      thumb.set_y(0);
      thumb.set_height(height());
    } else {
      thumb.set_x(0);
      thumb.set_width(width());
    }
    thumb_->SetBoundsRect(thumb);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    if (!hovered_) {
      return;
    }
    const SkColor ink = Ink();
    canvas->FillRect(GetLocalBounds(),
                     ink == SK_ColorWHITE ? kTrackDark : kTrackLight);
    // The track's inner edge, toward the content.
    canvas->FillRect(IsHorizontal() ? gfx::Rect(0, 0, width(), 1)
                                    : gfx::Rect(0, 0, 1, height()),
                     SkColorSetA(ink, kEdgeAlpha));
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetA(ink, kHoverAlpha));
    for (const bool start : {true, false}) {
      canvas->DrawPath(Arrow(start), flags);
    }
  }

 private:
  class Thumb : public views::BaseScrollBarThumb {
    METADATA_HEADER(Thumb, views::BaseScrollBarThumb)

   public:
    explicit Thumb(FluentScrollBar* bar)
        : views::BaseScrollBarThumb(bar), bar_(bar) {}

    // views::BaseScrollBarThumb:
    gfx::Size CalculatePreferredSize(
        const views::SizeBounds& available_size) const override {
      return gfx::Size(kTrack, kTrack);
    }

    void OnPaint(gfx::Canvas* canvas) override {
      const bool hovered = bar_->hovered();
      const int thickness = hovered ? kHoverThumb : kRestThumb;
      gfx::RectF thumb(GetLocalBounds());
      if (bar_->IsHorizontal()) {
        thumb = gfx::RectF(thumb.x(), height() - kInset - thickness,
                           thumb.width(), thickness);
      } else {
        thumb = gfx::RectF(width() - kInset - thickness, thumb.y(), thickness,
                           thumb.height());
      }
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(
          SkColorSetA(bar_->Ink(), hovered ? kHoverAlpha : kRestAlpha));
      canvas->DrawRoundRect(thumb, thickness / 2.0f, flags);
    }

   private:
    const raw_ptr<FluentScrollBar> bar_;
  };

  // The arrow at the start (up, or left) or the end.
  SkPath Arrow(bool start) const {
    SkPathBuilder path;
    if (IsHorizontal()) {
      const float y = height() - kInset - kArrowWidth;
      const float x = start ? kArrowEnd : width() - kArrowEnd - kArrowHeight;
      if (start) {
        path.moveTo(x + kArrowHeight, y);
        path.lineTo(x, y + kArrowWidth / 2.0f);
        path.lineTo(x + kArrowHeight, y + kArrowWidth);
      } else {
        path.moveTo(x, y);
        path.lineTo(x + kArrowHeight, y + kArrowWidth / 2.0f);
        path.lineTo(x, y + kArrowWidth);
      }
    } else {
      const float x = width() - kInset - kArrowWidth;
      const float y = start ? kArrowEnd : height() - kArrowEnd - kArrowHeight;
      if (start) {
        path.moveTo(x, y + kArrowHeight);
        path.lineTo(x + kArrowWidth / 2.0f, y);
        path.lineTo(x + kArrowWidth, y + kArrowHeight);
      } else {
        path.moveTo(x, y);
        path.lineTo(x + kArrowWidth / 2.0f, y + kArrowHeight);
        path.lineTo(x + kArrowWidth, y);
      }
    }
    path.close();
    return path.detach();
  }

  void SchedulePaintAll() {
    SchedulePaint();
    for (views::View* child : children()) {
      child->SchedulePaint();
    }
  }

  // Shows it without delay.
  void Show() {
    hide_timer_.Stop();
    ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
    settings.SetTransitionDuration(base::TimeDelta());
    layer()->SetOpacity(1);
  }

  // Fades it once nothing moves, unless the pointer is over it.
  void StartHideCountdown() {
    if (hovered_) {
      return;
    }
    hide_timer_.Start(
        FROM_HERE, kHideDelay,
        base::BindOnce(&FluentScrollBar::Hide, base::Unretained(this)));
  }

  void Hide() {
    ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
    settings.SetTransitionDuration(kFade);
    layer()->SetOpacity(0);
  }

  raw_ptr<views::BaseScrollBarThumb> thumb_ = nullptr;
  bool hovered_ = false;
  int offset_ = 0;
  base::OneShotTimer hide_timer_;
};

BEGIN_METADATA(FluentScrollBar, Thumb)
END_METADATA

BEGIN_METADATA(FluentScrollBar)
END_METADATA

}  // namespace

void TakeSystemScrollBars(views::ScrollView& view) {
  view.SetVerticalScrollBar(std::make_unique<FluentScrollBar>(
      views::ScrollBar::Orientation::kVertical));
  view.SetHorizontalScrollBar(std::make_unique<FluentScrollBar>(
      views::ScrollBar::Orientation::kHorizontal));
}

}  // namespace pane
