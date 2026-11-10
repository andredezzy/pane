// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/panels/app_icon_picker.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/dialog.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/design/text_button.h"
#include "pane/ui/view_ids.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"

namespace pane {

namespace {

// The dialog's content, 24 in from its sides.
constexpr int kContentWidth = AppIconPicker::kWidth - 2 * 24;
// The heading, 16 over the skies; the skies six a row, 4 apart either way;
// Done 16 and 8 under them.
constexpr int kColumns = 6;
constexpr int kGap = 4;
constexpr int kHeadingSpacing = 16;
constexpr int kDoneSpacing = 16 + 8;

// A tile: the icon at 56 points, 8 from its top, over its name, 6 under it, at
// 11 on a 16-point line, 8 from its bottom; 68 wide, rounded 15 for the
// pointer's fill.
constexpr int kTileWidth = 68;
constexpr int kTilePadding = 8;
constexpr int kIconSize = AppIconImages::kTileSize;
constexpr int kNameSpacing = 6;
constexpr TextStyle kName{11, weight::kRegular, 16};
constexpr int kTileHeight =
    kTilePadding + kIconSize + kNameSpacing + kName.line_height + kTilePadding;

// The ring around the chosen icon, as the profile sheet's swatch is
// (DESIGN.md › Shape): 2 points wide, 2 to 4 points out from the icon's body,
// which macOS lays out at 824 of 1024, its corners the system's own,
// continuous and 0.257 of its side (measured on macOS 27).
constexpr float kBody = kIconSize * 824.0f / 1024.0f;
constexpr float kBodyRadius = kBody * 0.257f;
constexpr float kRingOut = 4;
constexpr float kRingWidth = 2;

// A rounded rectangle with the system's continuous corners (SwiftUI's
// RoundedRectangle(style: .continuous), Core Animation's
// kCACornerCurveContinuous): each corner three cubics that ease from the
// straight edge into the curve, starting 1.528665 of the radius from the
// corner, as the iOS 7 icon's are drawn (PaintCode's measure of
// UIBezierPath's).
SkPath ContinuousRoundedRect(const gfx::RectF& rect, float r) {
  r = std::min(r, std::min(rect.width(), rect.height()) / 2 / 1.52866483f);
  const float x = rect.x();
  const float y = rect.y();
  const float right = rect.right();
  const float bottom = rect.bottom();
  const auto at = [](float edge, float sign, float r, float k) {
    return edge + sign * k * r;
  };
  SkPathBuilder path;
  path.moveTo(at(x, 1, r, 1.52866483f), y);
  path.lineTo(at(right, -1, r, 1.52866483f), y);
  path.cubicTo(at(right, -1, r, 1.08849296f), y, at(right, -1, r, 0.86840694f),
               y, at(right, -1, r, 0.63149379f), at(y, 1, r, 0.07491139f));
  path.cubicTo(at(right, -1, r, 0.37282383f), at(y, 1, r, 0.16905956f),
               at(right, -1, r, 0.16905956f), at(y, 1, r, 0.37282383f),
               at(right, -1, r, 0.07491139f), at(y, 1, r, 0.63149379f));
  path.cubicTo(right, at(y, 1, r, 0.86840694f), right, at(y, 1, r, 1.08849296f),
               right, at(y, 1, r, 1.52866483f));
  path.lineTo(right, at(bottom, -1, r, 1.52866483f));
  path.cubicTo(right, at(bottom, -1, r, 1.08849296f), right,
               at(bottom, -1, r, 0.86840694f), at(right, -1, r, 0.07491139f),
               at(bottom, -1, r, 0.63149379f));
  path.cubicTo(at(right, -1, r, 0.16905956f), at(bottom, -1, r, 0.37282383f),
               at(right, -1, r, 0.37282383f), at(bottom, -1, r, 0.16905956f),
               at(right, -1, r, 0.63149379f), at(bottom, -1, r, 0.07491139f));
  path.cubicTo(at(right, -1, r, 0.86840694f), bottom,
               at(right, -1, r, 1.08849296f), bottom,
               at(right, -1, r, 1.52866483f), bottom);
  path.lineTo(at(x, 1, r, 1.52866483f), bottom);
  path.cubicTo(at(x, 1, r, 1.08849296f), bottom, at(x, 1, r, 0.86840694f),
               bottom, at(x, 1, r, 0.63149379f),
               at(bottom, -1, r, 0.07491139f));
  path.cubicTo(at(x, 1, r, 0.37282383f), at(bottom, -1, r, 0.16905956f),
               at(x, 1, r, 0.16905956f), at(bottom, -1, r, 0.37282383f),
               at(x, 1, r, 0.07491139f), at(bottom, -1, r, 0.63149379f));
  path.cubicTo(x, at(bottom, -1, r, 0.86840694f), x,
               at(bottom, -1, r, 1.08849296f), x,
               at(bottom, -1, r, 1.52866483f));
  path.lineTo(x, at(y, 1, r, 1.52866483f));
  path.cubicTo(x, at(y, 1, r, 1.08849296f), x, at(y, 1, r, 0.86840694f),
               at(x, 1, r, 0.07491139f), at(y, 1, r, 0.63149379f));
  path.cubicTo(at(x, 1, r, 0.16905956f), at(y, 1, r, 0.37282383f),
               at(x, 1, r, 0.37282383f), at(y, 1, r, 0.16905956f),
               at(x, 1, r, 0.63149379f), at(y, 1, r, 0.07491139f));
  path.cubicTo(at(x, 1, r, 0.86840694f), y, at(x, 1, r, 1.08849296f), y,
               at(x, 1, r, 1.52866483f), y);
  path.close();
  return path.detach();
}

ControlLook TileLook() {
  ControlLook look(ControlShape::Squircle(radius::kLg));
  look.hover_fill = kPaneColorAccent;
  // A plain button's: the pointer's fill alone.
  look.press = SK_ColorTRANSPARENT;
  look.disabled_opacity = 1;
  return look;
}

}  // namespace

// One sky: its icon over its name, the chosen one ringed and named in the
// foreground, the others muted. The pointer over it fades its fill in.
class AppIconPicker::Tile : public views::Button {
  METADATA_HEADER(Tile, views::Button)

 public:
  Tile(model::AppIcon icon, base::RepeatingClosure pressed)
      : views::Button(std::move(pressed)), icon_(icon), look_(TileLook()) {
    SetID(kViewIdAppIconTile);
    highlight_ = std::make_unique<ControlHighlight>(
        this, look_.shape, look_.disabled_opacity,
        base::BindRepeating(&views::View::SchedulePaint,
                            base::Unretained(this)));
    SetBackground(std::make_unique<ControlBackground>(look_, highlight_.get()));
    name_ = AddChildView(
        MakeLabel(AppIconTitle(icon), kName, kPaneColorMutedForeground));
    name_->SetCanProcessEventsWithinSubtree(false);
    name_->SetElideBehavior(gfx::ELIDE_TAIL);
    GetViewAccessibility().SetName(AppIconTitle(icon));
  }

  model::AppIcon icon() const { return icon_; }

  void SetSelected(bool selected) {
    if (selected_ == selected && name_color_set_) {
      return;
    }
    selected_ = selected;
    name_color_set_ = true;
    name_->SetEnabledColor(selected ? kPaneColorForeground
                                    : kPaneColorMutedForeground);
    GetViewAccessibility().SetIsSelected(selected);
    SchedulePaint();
  }

  void SetThumbnail(const gfx::ImageSkia& thumbnail) {
    thumbnail_ = thumbnail;
    SchedulePaint();
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kTileWidth, kTileHeight);
  }

  void Layout(PassKey) override {
    name_->SetBounds(0, kTilePadding + kIconSize + kNameSpacing, width(),
                     kName.line_height);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::RectF icon((width() - kIconSize) / 2.0f, kTilePadding, kIconSize,
                          kIconSize);
    // At its own size, from the representation of the screen's scale.
    if (!thumbnail_.isNull()) {
      canvas->DrawImageInt(thumbnail_, static_cast<int>(icon.x()),
                           static_cast<int>(icon.y()));
    }
    const ui::ColorProvider* colors = GetColorProvider();
    if (!selected_ || !colors) {
      return;
    }
    // Over the icon, so its shadow leaves the ring even; drawn as SwiftUI's
    // strokeBorder draws it, inside its frame.
    const float side = kBody + 2 * kRingOut;
    gfx::RectF ring(icon.CenterPoint().x() - side / 2,
                    icon.CenterPoint().y() - side / 2, side, side);
    ring.Inset(kRingWidth / 2);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(kRingWidth);
    flags.setColor(colors->GetColor(kPaneColorForeground));
    canvas->DrawPath(
        ContinuousRoundedRect(ring, kBodyRadius + kRingOut - kRingWidth / 2),
        flags);
  }

 private:
  const model::AppIcon icon_;
  const ControlLook look_;
  bool selected_ = false;
  bool name_color_set_ = false;
  gfx::ImageSkia thumbnail_;
  std::unique_ptr<ControlHighlight> highlight_;
  raw_ptr<views::Label> name_ = nullptr;
};

BEGIN_METADATA(AppIconPicker, Tile)
END_METADATA

AppIconPicker::AppIconPicker(
    AppIconImages& images,
    model::AppIcon selection,
    base::RepeatingCallback<void(model::AppIcon)> choose,
    base::RepeatingClosure done)
    : images_(images) {
  heading_ = AddChildView(std::make_unique<DialogHeading>(
      u"App icon",
      u"Automatic follows the theme: Day when light, Night when dark."));
  for (const model::AppIcon icon : AllAppIcons()) {
    tiles_[icon] = AddChildView(
        std::make_unique<Tile>(icon, base::BindRepeating(choose, icon)));
  }
  // The key in its text form (VS15): without it, Return's arrow takes the
  // emoji font's color glyph.
  done_ = AddChildView(
      std::make_unique<KeyedButton>(std::move(done), u"Done", u"↩\uFE0E"));
  done_->SetID(kViewIdAppIconDone);
  SetSelection(selection);
  images_observation_.Observe(&images);
  // Every sky's, should Settings not have asked yet.
  images.LoadAll();
  OnAppIconThumbnailsChanged();
}

AppIconPicker::~AppIconPicker() = default;

void AppIconPicker::SetSelection(model::AppIcon selection) {
  for (auto& [icon, tile] : tiles_) {
    tile->SetSelected(icon == selection);
  }
}

std::vector<std::pair<std::string, const views::View*>>
AppIconPicker::NamedViews() const {
  std::vector<std::pair<std::string, const views::View*>> named;
  for (const auto& [icon, tile] : tiles_) {
    named.emplace_back(
        base::StrCat({"pane.settings.appIcon.", model::AppIconName(icon)}),
        tile.get());
  }
  named.emplace_back(std::string(ViewIdName(kViewIdAppIconDone)), done_.get());
  return named;
}

gfx::Size AppIconPicker::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int rows = (static_cast<int>(tiles_.size()) + kColumns - 1) / kColumns;
  return gfx::Size(kContentWidth, heading_->GetHeightForWidth(kContentWidth) +
                                      kHeadingSpacing + rows * kTileHeight +
                                      (rows - 1) * kGap + kDoneSpacing +
                                      done_->GetPreferredSize().height());
}

void AppIconPicker::Layout(PassKey) {
  const int heading = heading_->GetHeightForWidth(width());
  heading_->SetBounds(0, 0, width(), heading);
  const int top = heading + kHeadingSpacing;
  int index = 0;
  for (const model::AppIcon icon : AllAppIcons()) {
    const int column = index % kColumns;
    const int row = index / kColumns;
    tiles_[icon]->SetBounds(column * (kTileWidth + kGap),
                            top + row * (kTileHeight + kGap), kTileWidth,
                            kTileHeight);
    ++index;
  }
  const int rows = (index + kColumns - 1) / kColumns;
  const gfx::Size done = done_->GetPreferredSize();
  done_->SetBounds(width() - done.width(),
                   top + rows * kTileHeight + (rows - 1) * kGap + kDoneSpacing,
                   done.width(), done.height());
}

void AppIconPicker::OnAppIconThumbnailsChanged() {
  for (auto& [icon, tile] : tiles_) {
    tile->SetThumbnail(images_->Thumbnail(icon, AppIconImages::kTileSize));
  }
}

BEGIN_METADATA(AppIconPicker)
END_METADATA

}  // namespace pane
