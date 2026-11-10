// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/scroll_fade.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/views/controls/scroll_view.h"

namespace pane {

namespace {

// The Swift's mask stops: at each share of the way in from the edge, the
// list's alpha was share²; the overlay's is what that leaves.
constexpr std::array<float, 5> kStops = {0, 0.25f, 0.5f, 0.75f, 1};

}  // namespace

// One edge's overlay: from the color behind at the edge to clear, over its
// height, which grows and shrinks with the scroll.
class FadingScrollView::Fade : public views::View {
  METADATA_HEADER(Fade, views::View)

 public:
  Fade(ui::ColorVariant behind, bool from_top)
      : behind_(std::move(behind)), from_top_(from_top) {
    // Over the list, which scrolls on a layer of its own.
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors || height() <= 0) {
      return;
    }
    const SkColor4f behind =
        SkColor4f::FromColor(behind_.ResolveToSkColor(colors));
    std::array<SkColor4f, kStops.size()> stops;
    for (size_t i = 0; i < kStops.size(); ++i) {
      stops[i] = behind;
      stops[i].fA *= 1 - kStops[i] * kStops[i];
    }
    const float edge = from_top_ ? 0 : height();
    const std::array<SkPoint, 2> points = {SkPoint::Make(0, edge),
                                           SkPoint::Make(0, height() - edge)};
    cc::PaintFlags flags;
    flags.setShader(cc::PaintShader::MakeLinearGradient(
        points.data(), stops.data(), kStops.data(), kStops.size(),
        SkTileMode::kClamp));
    canvas->DrawRect(GetLocalBounds(), flags);
  }

 private:
  const ui::ColorVariant behind_;
  const bool from_top_;
};

BEGIN_METADATA(FadingScrollView, Fade)
END_METADATA

FadingScrollView::FadingScrollView(ui::ColorVariant behind,
                                   bool fades_top,
                                   bool fades_bottom) {
  scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->SetBackgroundColor(std::nullopt);
  // The fades are its edges' only mark: Views' separator where the content
  // runs past an edge, Material's blue, stays off (it sat under the fade).
  scroll_view_->SetDrawOverflowIndicator(false);
  scrolled_ = scroll_view_->AddContentsScrolledCallback(base::BindRepeating(
      &FadingScrollView::UpdateFades, base::Unretained(this)));
  if (fades_top) {
    top_ = AddChildView(std::make_unique<Fade>(behind, /*from_top=*/true));
  }
  if (fades_bottom) {
    bottom_ = AddChildView(std::make_unique<Fade>(behind, /*from_top=*/false));
  }
}

FadingScrollView::~FadingScrollView() = default;

int FadingScrollView::top_fade() const {
  return top_ ? top_->height() : 0;
}

int FadingScrollView::bottom_fade() const {
  return bottom_ ? bottom_->height() : 0;
}

void FadingScrollView::Layout(PassKey) {
  scroll_view_->SetBoundsRect(GetLocalBounds());
  // The list's contents can be set or swapped after: follow the one there.
  if (views::View* contents = scroll_view_->contents();
      contents && !contents_.IsObservingSource(contents)) {
    contents_.Reset();
    contents_.Observe(contents);
  }
  UpdateFades();
}

void FadingScrollView::OnViewBoundsChanged(views::View* view) {
  UpdateFades();
}

void FadingScrollView::UpdateFades() {
  const views::View* contents = scroll_view_->contents();
  const gfx::Rect visible = scroll_view_->GetVisibleRect();
  const int scrolled = visible.y();
  const int below =
      contents ? contents->height() - visible.height() - scrolled : 0;
  // Where the list's own viewport is, in this view.
  const gfx::Rect viewport = scroll_view_->GetContentsBounds();
  if (top_) {
    const int height = std::clamp(scrolled, 0, kFade);
    top_->SetBounds(viewport.x(), viewport.y(), viewport.width(), height);
  }
  if (bottom_) {
    const int height = std::clamp(below, 0, kFade);
    bottom_->SetBounds(viewport.x(), viewport.bottom() - height,
                       viewport.width(), height);
  }
}

BEGIN_METADATA(FadingScrollView)
END_METADATA

}  // namespace pane
