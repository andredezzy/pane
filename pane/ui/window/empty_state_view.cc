// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/empty_state_view.h"

#include <algorithm>
#include <memory>

#include "cc/paint/paint_flags.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/text.h"
#include "pane/ui/view_ids.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"

namespace pane {

namespace {

// The mark, 40 points across, and the line under it, 12 points below: 12 on
// a 16-point line, muted.
constexpr int kMarkSize = 40;
constexpr int kSpacing = 12;
constexpr TextStyle kLine{12, weight::kRegular, 16};

// Pane's mark, the 24-unit icon: an 18-unit square at 3, its corners 4, split
// down the middle at 12, each line 1.5 units, in the foreground.
class PaneMark : public views::View {
  METADATA_HEADER(PaneMark, views::View)

 public:
  PaneMark() {
    GetViewAccessibility().SetRole(ax::mojom::Role::kImage);
    GetViewAccessibility().SetName(u"Pane");
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kMarkSize, kMarkSize);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const float unit = width() / 24.0f;
    // SVG's rect, whose corners are circles: Paper's, and within a fifth of
    // a pixel of SwiftUI's continuous ones at this radius (ADR 0057).
    SkPathBuilder mark;
    mark.addRRect(SkRRect::MakeRectXY(
        SkRect::MakeXYWH(3 * unit, 3 * unit, 18 * unit, 18 * unit), 4 * unit,
        4 * unit));
    mark.moveTo(12 * unit, 3 * unit);
    mark.lineTo(12 * unit, 21 * unit);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.5f * unit);
    flags.setColor(colors->GetColor(kPaneColorForeground));
    canvas->DrawPath(mark.detach(), flags);
  }
};

BEGIN_METADATA(PaneMark)
END_METADATA

}  // namespace

EmptyStateView::EmptyStateView() {
  SetID(kViewIdEmpty);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  mark_ = AddChildView(std::make_unique<PaneMark>());
  label_ = AddChildView(MakeLabel(u"Open a profile to start browsing", kLine,
                                  kPaneColorMutedForeground));
}

EmptyStateView::~EmptyStateView() = default;

// The two, one above the other, centered in the panel.
void EmptyStateView::Layout(PassKey) {
  const gfx::Size line = label_->GetPreferredSize();
  const int height = kMarkSize + kSpacing + line.height();
  const int top = (this->height() - height) / 2;
  mark_->SetBounds((width() - kMarkSize) / 2, top, kMarkSize, kMarkSize);
  label_->SetBounds((width() - line.width()) / 2, top + kMarkSize + kSpacing,
                    std::min(line.width(), width()), line.height());
}

BEGIN_METADATA(EmptyStateView)
END_METADATA

}  // namespace pane
