// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/drag_copy.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "cc/paint/draw_looper.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/sidebar/profile_row.h"
#include "pane/ui/sidebar/row_button.h"
#include "pane/ui/sidebar/tab_row.h"
#include "pane/ui/tabs/tab_favicon.h"
#include "pane/ui/view_ids.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"

namespace pane {

namespace {

// The shadow: black at 12%, in both appearances, 4 points down, a Gaussian
// of 8 (SwiftUI's shadow radius is the Gaussian's sigma).
constexpr SkColor4f kShadow = {0, 0, 0, 0.12f};
constexpr float kShadowDown = 4;
constexpr float kShadowSigma = 8;

}  // namespace

// What the card shows, in its own coordinates, laid out as the row it copies:
// a profile's mark, name, dot, and count, 8 in; a tab's favicon 7 in and its
// title 10 after, to 8 from the end.
class DragCopy::Contents : public views::View {
  METADATA_HEADER(Contents, views::View)

 public:
  Contents() {
    title_ = AddChildView(
        MakeLabel(u"", text::kSidebar, kPaneColorAccentForeground));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    count_ =
        AddChildView(MakeLabel(u"", text::kSidebar, kPaneColorMutedForeground));
    count_->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
    count_->SetVisible(false);
  }

  void SetProfile(const SidebarProfile& profile) {
    profile_ = profile;
    tab_.reset();
    StyleLabel(*title_, kProfileName, kPaneColorAccentForeground);
    title_->SetText(profile.name);
    // The count while collapsed, of a profile with tabs.
    const bool shows_count = !profile.expanded && profile.tab_count > 0;
    count_->SetVisible(shows_count);
    if (shows_count) {
      count_->SetText(base::NumberToString16(profile.tab_count));
    }
    InvalidateLayout();
    SchedulePaint();
  }

  void SetTab(const SidebarTab& tab) {
    tab_ = tab;
    profile_.reset();
    StyleLabel(*title_, text::kSidebar, kPaneColorAccentForeground);
    title_->SetText(TabTitle(tab));
    count_->SetVisible(false);
    InvalidateLayout();
    SchedulePaint();
  }

  // views::View:
  void Layout(PassKey) override {
    if (profile_) {
      const ProfileHeaderPlaces places = PlaceProfileHeader(
          size(), ProfileMarkWidth(profile_->shown ? 1 : 0),
          count_->GetVisible() ? count_->GetPreferredSize().width() : 0,
          profile_->proxy.has_value());
      count_->SetBoundsRect(places.count);
      dot_x_ = places.dot_x;
      title_->SetBoundsRect(places.name);
      return;
    }
    const int start =
        RowButton::kLeading + RowButton::kIconSize + RowButton::kIconSpacing;
    title_->SetBounds(start, 0,
                      std::max(width() - RowButton::kTrailing - start, 0),
                      height());
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    if (profile_) {
      PaintProfileMark(*canvas, height(), profile_->color,
                       profile_->shown ? 1 : 0);
      if (profile_->proxy) {
        PaintProxyDot(*canvas, *colors, dot_x_, height(), *profile_->proxy);
      }
      return;
    }
    if (tab_) {
      PaintFavicon(
          *canvas,
          gfx::RectF(RowButton::kLeading, (height() - RowButton::kIconSize) / 2,
                     RowButton::kIconSize, RowButton::kIconSize),
          tab_->favicon, FaviconPlaceholder::kGlobe,
          colors->GetColor(kPaneColorAccentForeground));
    }
  }

 private:
  std::optional<SidebarProfile> profile_;
  std::optional<SidebarTab> tab_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> count_ = nullptr;
  float dot_x_ = 0;
};

BEGIN_METADATA(DragCopy, Contents)
END_METADATA

DragCopy::DragCopy() {
  SetID(kViewIdDragCopy);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetCanProcessEventsWithinSubtree(false);
  // The row itself stays in the list for VoiceOver: the copy and its text
  // are hidden from it.
  GetViewAccessibility().SetIsLeaf(true);
  GetViewAccessibility().SetIsIgnored(true);
  contents_ = AddChildView(std::make_unique<Contents>());
}

DragCopy::~DragCopy() = default;

void DragCopy::SetProfile(const SidebarProfile& profile) {
  contents_->SetProfile(profile);
}

void DragCopy::SetTab(const SidebarTab& tab) {
  contents_->SetTab(tab);
}

void DragCopy::SetCard(const gfx::RectF& card) {
  // On whole points, and the rest by the layer, so a move paints nothing.
  const gfx::Point whole(std::floor(card.x()), std::floor(card.y()));
  gfx::Rect bounds(
      whole, gfx::Size(std::round(card.width()), std::round(card.height())));
  bounds.Outset(kShadowReach);
  SetBoundsRect(bounds);
  if (layer()) {
    layer()->SetTransform(gfx::Transform::MakeTranslation(
        card.x() - whole.x(), card.y() - whole.y()));
  }
}

gfx::RectF DragCopy::card() const {
  gfx::RectF card(bounds());
  card.Inset(kShadowReach);
  if (layer()) {
    card.Offset(layer()->transform().To2dTranslation());
  }
  return card;
}

const views::View* DragCopy::card_view() const {
  return contents_;
}

void DragCopy::Layout(PassKey) {
  gfx::Rect card = GetLocalBounds();
  card.Inset(kShadowReach);
  contents_->SetBoundsRect(card);
}

void DragCopy::OnPaint(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  gfx::RectF card(GetLocalBounds());
  card.Inset(kShadowReach);
  // The card's fill over its shadow.
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(colors->GetColor(kPaneColorCard));
  cc::DrawLooperBuilder shadow;
  shadow.AddUnmodifiedContent();
  shadow.AddShadow({0, kShadowDown}, kShadowSigma, kShadow,
                   cc::DrawLooper::kOverrideAlphaFlag);
  flags.setLooper(shadow.Detach());
  canvas->DrawPath(SquirclePath(card, radius::kLg), flags);
  // Its border, a point wide just inside the edge.
  cc::PaintFlags border;
  border.setAntiAlias(true);
  border.setStyle(cc::PaintFlags::kStroke_Style);
  border.setStrokeWidth(1);
  border.setColor(colors->GetColor(kPaneColorBorder));
  canvas->DrawPath(SquircleStrokePath(card, radius::kLg, 1), border);
}

BEGIN_METADATA(DragCopy)
END_METADATA

}  // namespace pane
