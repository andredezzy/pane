// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/tabs/tab_switcher_view.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "cc/paint/draw_looper.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/tabs/tab_favicon.h"
#include "pane/ui/view_ids.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_solid_color.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/outsets_f.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/render_text.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/accessibility/ax_virtual_view.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_targeter.h"

namespace pane {

namespace {

// The panel: 320 wide, 10 inside its 1-point ring (CSS's p-2.5 within its
// border), its groups 8 apart; rounded 22, popover's fill, its ring the
// foreground at 10%, its shadow 6 down, a Gaussian of 10 (SwiftUI's shadow
// radius is the Gaussian's sigma).
constexpr float kWidth = 320;
constexpr float kPadding = 11;
constexpr float kGroupSpacing = 8;
constexpr gfx::Vector2dF kShadowOffset(0, 6);
constexpr float kShadowSigma = 10;

// A group's header: 6 above and 4 below a 16.5-point line, 12 in; its
// profile's 8-point dot, and 8 after it the name, 11 medium, muted.
constexpr float kHeaderTop = 6;
constexpr float kHeaderLine = 16.5;
constexpr float kHeaderHeight = kHeaderTop + kHeaderLine + 4;
constexpr float kHeaderInset = 12;
constexpr float kDot = 8;
constexpr float kDotGap = 8;
constexpr TextStyle kHeaderText{11, weight::kMedium, 16};

// A row: 10 above and below a 20-point line; the 16-point favicon 28 in, the
// title 10 after it, 14 regular at 80% of the foreground, to 12 from the end.
constexpr float kRowHeight = 40;
constexpr float kRowLeading = 28;
constexpr float kRowTrailing = 12;
constexpr float kFavicon = 16;
constexpr float kFaviconGap = 10;
constexpr TextStyle kRowText{14, weight::kRegular, 20};
constexpr float kRowTextOpacity = 0.8f;

// The selected row: its profile's color at 0x24 (14%), rounded 15, ringed
// at 0x66 (40%) a point outside it, as CSS's ring is.
constexpr SkAlpha kSelectedFill = 0x24;
constexpr SkAlpha kSelectedRing = 0x66;

// The placeholder of a tab with no favicon: the foreground at 10%.
constexpr float kPlaceholderOpacity = 0.1f;

// How far the shadow reaches past the panel: three of its sigma, its offset
// down.
constexpr gfx::OutsetsF kShadowReach =
    gfx::OutsetsF::TLBR(3 * kShadowSigma - kShadowOffset.y(),
                        3 * kShadowSigma,
                        3 * kShadowSigma + kShadowOffset.y(),
                        3 * kShadowSigma);

std::unique_ptr<gfx::RenderText> TextLine(const std::u16string& text,
                                          const TextStyle& style,
                                          float width) {
  // As a views::Label sets its text (Label::CreateRenderText), styled as
  // StyleLabel styles one: its line box, centered in it, ending in an
  // ellipsis, grayscale.
  std::unique_ptr<gfx::RenderText> line = gfx::RenderText::CreateRenderText();
  line->SetText(text);
  line->SetFontList(SystemFont(style.size, style.weight));
  line->SetCursorEnabled(false);
  line->SetMultiline(false);
  line->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  line->SetVerticalAlignment(gfx::ALIGN_MIDDLE);
  line->SetElideBehavior(gfx::ELIDE_TAIL);
  line->SetMinLineHeight(style.line_height);
  line->set_subpixel_rendering_suppressed(true);
  line->SetDisplayRect(
      gfx::Rect(0, 0, std::floor(std::max(width, 0.0f)), style.line_height));
  return line;
}

// Draws `line` with its line box's top left at `origin`, a fraction of a point
// included.
void DrawLine(gfx::Canvas& canvas,
              gfx::RenderText& line,
              const gfx::PointF& origin) {
  gfx::ScopedCanvas scoped(&canvas);
  canvas.sk_canvas()->translate(origin.x(), origin.y());
  line.Draw(&canvas);
}

}  // namespace

TabSwitcherView::Group::Group() = default;
TabSwitcherView::Group::Group(Group&&) = default;
TabSwitcherView::Group& TabSwitcherView::Group::operator=(Group&&) = default;
TabSwitcherView::Group::~Group() = default;

// The panel and its shadow, on a layer of their own over the scrim, painted
// by the switcher in its own points.
class TabSwitcherView::Panel : public views::View {
  METADATA_HEADER(Panel, views::View)

 public:
  explicit Panel(TabSwitcherView* switcher) : switcher_(switcher) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    gfx::ScopedCanvas scoped(canvas);
    canvas->Translate(-bounds().OffsetFromOrigin());
    switcher_->PaintPanel(canvas);
  }

 private:
  const raw_ptr<TabSwitcherView> switcher_;
};

BEGIN_METADATA(TabSwitcherView, Panel)
END_METADATA

TabSwitcherView::Line::Line() = default;
TabSwitcherView::Line::Line(Line&&) = default;
TabSwitcherView::Line& TabSwitcherView::Line::operator=(Line&&) = default;
TabSwitcherView::Line::~Line() = default;

TabSwitcherView::TabSwitcherView() {
  SetID(kViewIdSwitcher);
  // The scrim: a color the compositor draws, with nothing to paint.
  SetPaintToLayer(ui::LAYER_SOLID_COLOR);
  layer()->SetFillsBoundsOpaquely(false);
  panel_view_ = AddChildView(std::make_unique<Panel>(this));
  SetEventTargeter(std::make_unique<views::ViewTargeter>(this));
  GetViewAccessibility().SetRole(ax::mojom::Role::kListBox);
  GetViewAccessibility().SetName(u"Tab switcher");
}

TabSwitcherView::~TabSwitcherView() = default;

void TabSwitcherView::SetGroups(std::vector<Group> groups, int selected) {
  GetViewAccessibility().RemoveAllVirtualChildViews();
  groups_ = std::move(groups);
  headers_.clear();
  rows_.clear();
  row_groups_.clear();
  for (size_t group = 0; group < groups_.size(); ++group) {
    Line header;
    header.text =
        TextLine(groups_[group].name, kHeaderText,
                 kWidth - 2 * kPadding - 2 * kHeaderInset - kDot - kDotGap);
    headers_.push_back(std::move(header));
    for (const Tab& tab : groups_[group].tabs) {
      Line row;
      row.text = TextLine(tab.title, kRowText,
                          kWidth - 2 * kPadding - kRowLeading - kFavicon -
                              kFaviconGap - kRowTrailing);
      row.tab = &tab;
      auto accessible = std::make_unique<views::AXVirtualView>();
      accessible->SetRole(ax::mojom::Role::kListBoxOption);
      accessible->SetName(tab.title);
      row.accessible = accessible.get();
      GetViewAccessibility().AddVirtualChildView(std::move(accessible));
      rows_.push_back(std::move(row));
      row_groups_.push_back(group);
    }
  }
  selected_ = std::clamp(selected, 0, std::max(int(rows_.size()) - 1, 0));
  for (size_t index = 0; index < rows_.size(); ++index) {
    rows_[index].accessible->SetIsSelected(int(index) == selected_);
  }
  Tint();
  Place();
  panel_view_->SchedulePaint();
}

void TabSwitcherView::SetSelected(int selected) {
  if (rows_.empty()) {
    return;
  }
  selected = std::clamp(selected, 0, int(rows_.size()) - 1);
  if (selected == selected_) {
    return;
  }
  // At once, and only the two rows draw again.
  rows_[selected_].accessible->SetIsSelected(false);
  RepaintRow(selected_);
  selected_ = selected;
  rows_[selected_].accessible->SetIsSelected(true);
  RepaintRow(selected_);
}

std::vector<std::pair<std::string, gfx::Rect>> TabSwitcherView::NamedFrames()
    const {
  std::vector<std::pair<std::string, gfx::Rect>> frames;
  frames.emplace_back(std::string(ViewIdName(kViewIdSwitcher)),
                      gfx::ToEnclosingRect(panel_));
  for (const Line& row : rows_) {
    frames.emplace_back("pane.switcher.tab." + row.tab->id,
                        gfx::ToEnclosingRect(row.bounds));
  }
  return frames;
}

float TabSwitcherView::Scale() const {
  return layer() ? layer()->device_scale_factor() : 1;
}

// SwiftUI's: the panel in the window's middle, its origin on the screen's
// pixels, and its groups and rows at their offsets in it, half points
// included.
void TabSwitcherView::Place() {
  float height = 2 * kPadding;
  for (size_t group = 0; group < groups_.size(); ++group) {
    height += kHeaderHeight + kRowHeight * groups_[group].tabs.size() +
              (group ? kGroupSpacing : 0);
  }
  const float scale = Scale();
  const auto on_pixels = [&](float value) {
    return std::round(value * scale) / scale;
  };
  panel_ = gfx::RectF(on_pixels((width() - kWidth) / 2),
                      on_pixels((this->height() - height) / 2), kWidth, height);

  float y = panel_.y() + kPadding;
  size_t row = 0;
  for (size_t group = 0; group < groups_.size(); ++group) {
    if (group) {
      y += kGroupSpacing;
    }
    headers_[group].bounds = gfx::RectF(panel_.x() + kPadding, y,
                                        kWidth - 2 * kPadding, kHeaderHeight);
    y += kHeaderHeight;
    for (size_t tab = 0; tab < groups_[group].tabs.size(); ++tab, ++row) {
      rows_[row].bounds = gfx::RectF(panel_.x() + kPadding, y,
                                     kWidth - 2 * kPadding, kRowHeight);
      rows_[row].accessible->SetBounds(rows_[row].bounds);
      y += kRowHeight;
    }
  }
  gfx::RectF shadowed = panel_;
  shadowed.Outset(kShadowReach);
  panel_view_->SetBoundsRect(gfx::ToEnclosingRect(shadowed));
}

void TabSwitcherView::Tint() {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  for (Line& header : headers_) {
    header.text->SetColor(colors->GetColor(kPaneColorMutedForeground));
  }
  const SkColor foreground = colors->GetColor(kPaneColorForeground);
  for (Line& row : rows_) {
    row.text->SetColor(SkColorSetA(
        foreground, AlphaOf(kRowTextOpacity * SkColorGetA(foreground) / 255)));
  }
}

gfx::RectF TabSwitcherView::RowRingBounds(int index) const {
  gfx::RectF ring = rows_[index].bounds;
  // The ring a point outside, and its edge's antialiasing.
  ring.Outset(2);
  return ring;
}

void TabSwitcherView::RepaintRow(int index) {
  gfx::Rect rect = gfx::ToEnclosingRect(RowRingBounds(index));
  rect.Offset(-panel_view_->bounds().OffsetFromOrigin());
  panel_view_->SchedulePaintInRect(rect);
}

void TabSwitcherView::Layout(PassKey) {
  Place();
}

// On another screen, the panel goes on that screen's pixels.
void TabSwitcherView::OnDeviceScaleFactorChanged(
    float old_device_scale_factor,
    float new_device_scale_factor) {
  views::View::OnDeviceScaleFactorChanged(old_device_scale_factor,
                                          new_device_scale_factor);
  InvalidateLayout();
}

void TabSwitcherView::OnThemeChanged() {
  views::View::OnThemeChanged();
  // The scrim, black in both appearances.
  layer()->AsSolidColor()->SetColor(
      SkColor4f::FromColor(GetColorProvider()->GetColor(kPaneColorSheetScrim)));
  Tint();
  panel_view_->SchedulePaint();
}

void TabSwitcherView::PaintPanel(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  // The panel on its shadow.
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(colors->GetColor(kPaneColorPopover));
  cc::DrawLooperBuilder shadow;
  shadow.AddUnmodifiedContent();
  shadow.AddShadow(
      {kShadowOffset.x(), kShadowOffset.y()}, kShadowSigma,
      SkColor4f::FromColor(colors->GetColor(kPaneColorPopoverShadow)),
      cc::DrawLooper::kOverrideAlphaFlag);
  flags.setLooper(shadow.Detach());
  canvas->DrawPath(SquirclePath(panel_, radius::kXl), flags);

  // The selected row, in its profile's color.
  if (!rows_.empty()) {
    const Line& selected = rows_[selected_];
    const SkColor color = groups_[row_groups_[selected_]].color;
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(SkColorSetA(color, kSelectedFill));
    canvas->DrawPath(SquirclePath(selected.bounds, radius::kLg), fill);
    gfx::RectF ring = selected.bounds;
    ring.Outset(1);
    cc::PaintFlags line;
    line.setAntiAlias(true);
    line.setStyle(cc::PaintFlags::kStroke_Style);
    line.setStrokeWidth(1);
    line.setColor(SkColorSetA(color, kSelectedRing));
    canvas->DrawPath(SquircleStrokePath(ring, radius::kLg + 1, 1), line);
  }

  // Each group's dot, its frame on the screen's pixels as SwiftUI snaps a
  // view's, and its name.
  const float scale = Scale();
  for (size_t group = 0; group < groups_.size(); ++group) {
    const gfx::RectF& header = headers_[group].bounds;
    const float line_top = header.y() + kHeaderTop;
    const float dot_top =
        std::round((line_top + (kHeaderLine - kDot) / 2) * scale) / scale;
    cc::PaintFlags dot;
    dot.setAntiAlias(true);
    dot.setColor(groups_[group].color);
    canvas->DrawCircle(
        gfx::PointF(header.x() + kHeaderInset + kDot / 2, dot_top + kDot / 2),
        kDot / 2, dot);
    // Its whole-point line box, centered on the 16.5-point one.
    DrawLine(
        *canvas, *headers_[group].text,
        gfx::PointF(header.x() + kHeaderInset + kDot + kDotGap,
                    line_top + (kHeaderLine - kHeaderText.line_height) / 2));
  }

  // Each tab's favicon and title.
  const SkColor placeholder = SkColorSetA(
      colors->GetColor(kPaneColorForeground), AlphaOf(kPlaceholderOpacity));
  for (const Line& row : rows_) {
    const gfx::RectF& bounds = row.bounds;
    PaintFavicon(*canvas,
                 gfx::RectF(bounds.x() + kRowLeading,
                            bounds.y() + (kRowHeight - kFavicon) / 2, kFavicon,
                            kFavicon),
                 row.tab->favicon, FaviconPlaceholder::kSquare, placeholder);
    DrawLine(*canvas, *row.text,
             gfx::PointF(bounds.x() + kRowLeading + kFavicon + kFaviconGap,
                         bounds.y() + (kRowHeight - kRowText.line_height) / 2));
  }

  // The panel's ring, over what it holds.
  cc::PaintFlags ring;
  ring.setAntiAlias(true);
  ring.setStyle(cc::PaintFlags::kStroke_Style);
  ring.setStrokeWidth(1);
  ring.setColor(colors->GetColor(kPaneColorPopoverRing));
  canvas->DrawPath(SquircleStrokePath(panel_, radius::kXl, 1), ring);
}

bool TabSwitcherView::DoesIntersectRect(const views::View* target,
                                        const gfx::Rect& rect) const {
  return panel_.Intersects(gfx::RectF(rect));
}

BEGIN_METADATA(TabSwitcherView)
END_METADATA

}  // namespace pane
