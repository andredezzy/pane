// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sheets/profile_sheet.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "cc/paint/paint_flags.h"
#include "pane/browser/proxy_test.h"
#include "pane/model/host_platform.h"
#include "pane/model/profile_form.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/field.h"
#include "pane/ui/design/focus_ring.h"
#include "pane/ui/design/icon_button.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/menu_button.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/placement.h"
#include "pane/ui/design/scroll.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/switch.h"
#include "pane/ui/design/text.h"
#include "pane/ui/design/text_button.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/sheets/platform_logo.h"
#include "pane/ui/view_ids.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/clip_recorder.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/paint_recorder.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/render_text.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/paint_info.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_targeter.h"
#include "ui/views/view_targeter_delegate.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The panel (Paper 05.4): its header 16 over and under its 24-point row, 20
// in; the body 20 in, its parts 18 apart, the name and the color 16; the
// footer 16 over and under its buttons, 20 in, 8 between them.
constexpr int kInset = 20;
constexpr int kHeaderPadding = 16;
constexpr int kHeaderRow = 24;
constexpr int kHeaderHeight = kHeaderPadding + kHeaderRow + kHeaderPadding;
constexpr int kSectionSpacing = 18;
constexpr int kIdentitySpacing = 16;
constexpr int kFooterPadding = 16;
constexpr int kButtonSpacing = 8;
constexpr int kFooterHeight =
    kFooterPadding + button_size::kRegular.height + kFooterPadding;
constexpr TextStyle kTitle{14, weight::kMedium, 20};
constexpr int kCloseSize = 24;
// A form's label, 12 medium on a 16-point line, 8 over its field.
constexpr int kLabelSpacing = 8;
// A section's label, 13 on an 18-point line, 15 in, 8 over its card.
constexpr TextStyle kSectionTitle = text::kSectionTitle;
constexpr int kSectionTitleInset = 15;
constexpr int kSectionTitleSpacing = 8;
// A card: rounded 15, its rows inside its 1-point border.
constexpr int kCardBorder = 1;
// A card's row (the Swift's sheetRowPadding): 15 in, 14 from its end, 12
// over and under, the proxy's 14 under.
constexpr gfx::Insets kRowPadding = gfx::Insets::TLBR(12, 15, 12, 14);
constexpr gfx::Insets kProxyPadding = gfx::Insets::TLBR(12, 15, 14, 14);
// A switch's row: its labels 12 over and under and 15 in, 16 from the track
// (8 of their own and the switch's 8), the track 14 from the card's edge,
// outside the button.
constexpr gfx::Insets kSwitchLabels = gfx::Insets::TLBR(12, 15, 12, 8);
constexpr int kSwitchTrailing = 14;
// A row's labels: the title on 18-point lines, 2 over its note on 16-point
// ones.
constexpr int kNoteSpacing = 2;
// The swatches: 24 points, rounded 11, 8 apart; the chosen one ringed with
// the card 2 out and its color 3 out, the rings' radii the swatch's grown by
// as much (15 and 17), in room 3 over and under the row.
constexpr int kSwatch = 24;
constexpr int kSwatchSpacing = 8;
constexpr int kSwatchRing = 3;
// The platforms' tiles: 72 tall, 8 apart, rounded 11, a logo's 26-point box
// 4 over the name's 16-point line, the two in the tile's middle.
constexpr int kTileHeight = 72;
constexpr int kTileSpacing = 8;
constexpr int kLogoBox = 26;
constexpr int kLogoSpacing = 4;
constexpr TextStyle kTileName{12, weight::kMedium, 16};
// Under the tiles, 10 apart, the warning: 8 and 10 inside its fill rounded
// 9, its icon 14 points, 1 down, 8 from its words.
constexpr int kWarningSpacing = 10;
constexpr gfx::Insets kWarningPadding = gfx::Insets::VH(8, 10);
constexpr int kWarningIcon = 14;
constexpr int kWarningIconSpacing = 8;
// The zone's row: its labels, 16, its menu.
constexpr int kZoneSpacing = 16;
// The proxy's rows, 8 apart; the test 4 further down, 10 from its answer,
// whose dot is 6 points, 6 from its words.
constexpr int kProxySpacing = 8;
constexpr int kTestTop = 4;
constexpr int kTestSpacing = 10;
constexpr int kDot = 6;
constexpr int kDotSpacing = 6;
// The address: the type, then the host 12 in, then the port, 65 wide with its
// 12 either side, each apart by a 1-point rule.
constexpr int kCellPadding = 12;
constexpr int kPortWidth = 65;
constexpr TextStyle kFieldText{13, weight::kRegular, 18};
constexpr TextStyle kNote = text::kRowDescription;
constexpr TextStyle kFormLabel{12, weight::kMedium, 16};
// What Save brings into view of the address: the field, and 12 clear of the
// rules over and under it.
constexpr int kRevealMargin = 12;

constexpr std::array<model::ProfileColor, 9> kColors = {
    model::ProfileColor::kBlue,    model::ProfileColor::kRose,
    model::ProfileColor::kEmerald, model::ProfileColor::kAmber,
    model::ProfileColor::kViolet,  model::ProfileColor::kOrange,
    model::ProfileColor::kTeal,    model::ProfileColor::kFuchsia,
    model::ProfileColor::kZinc};

constexpr std::array<model::Platform, 3> kPlatforms = {
    model::Platform::kWindows, model::Platform::kMacOS,
    model::Platform::kLinux};

constexpr std::array<model::ProxyType, 4> kProxyTypes = {
    model::ProxyType::kHttp, model::ProxyType::kHttps,
    model::ProxyType::kSocks4, model::ProxyType::kSocks5};

std::u16string ProxyTypeTitle(model::ProxyType type) {
  switch (type) {
    case model::ProxyType::kHttp:
      return u"HTTP";
    case model::ProxyType::kHttps:
      return u"HTTPS";
    case model::ProxyType::kSocks4:
      return u"SOCKS4";
    case model::ProxyType::kSocks5:
      return u"SOCKS5";
  }
}

// "Blue": a swatch's name, as VoiceOver reads it.
std::u16string ColorTitle(model::ProfileColor color) {
  std::u16string name = base::UTF8ToUTF16(model::ColorName(color));
  for (size_t index = 1; index < name.size(); ++index) {
    name[index] = base::ToLowerASCII(name[index]);
  }
  return name;
}

std::string ColorKey(model::ProfileColor color) {
  return base::ToLowerASCII(model::ColorName(color));
}

// What a fingerprint of the platform claims the computer is.
std::u16string ClaimedDevice(model::Platform platform) {
  switch (platform) {
    case model::Platform::kWindows:
      return u"Windows PC";
    case model::Platform::kMacOS:
      return u"Mac";
    case model::Platform::kLinux:
      return u"Linux PC";
  }
}

std::u16string Host() {
  return base::UTF8ToUTF16(model::HostDeviceName());
}

std::unique_ptr<views::Label> Text(const std::u16string& text,
                                   const TextStyle& style,
                                   ui::ColorVariant color,
                                   bool wraps = false) {
  std::unique_ptr<views::Label> label = MakeLabel(text, style, color);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetMultiLine(wraps);
  return label;
}

// Views stacked one under another across the width, `spacing` apart, inside
// `insets`, each as tall as it needs at that width; a hidden one takes no
// room.
class Column : public views::View {
  METADATA_HEADER(Column, views::View)

 public:
  explicit Column(gfx::Insets insets = gfx::Insets(), int spacing = 0)
      : insets_(insets), spacing_(spacing) {}

  int HeightFor(int width) const {
    const int inner = std::max(width - insets_.width(), 0);
    int height = insets_.height();
    bool first = true;
    for (const views::View* child : children()) {
      if (!child->GetVisible()) {
        continue;
      }
      height += (first ? 0 : spacing_) + child->GetHeightForWidth(inner);
      first = false;
    }
    return height;
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int width = available_size.width().is_bounded()
                          ? available_size.width().value()
                          : ProfileSheet::kWidth;
    return gfx::Size(width, HeightFor(width));
  }

  void Layout(PassKey) override {
    const int inner = std::max(width() - insets_.width(), 0);
    int y = insets_.top();
    bool first = true;
    for (views::View* child : children()) {
      if (!child->GetVisible()) {
        continue;
      }
      y += first ? 0 : spacing_;
      const int height = child->GetHeightForWidth(inner);
      child->SetBounds(insets_.left(), y, inner, height);
      y += height;
      first = false;
    }
  }

  void ChildPreferredSizeChanged(views::View* child) override {
    PreferredSizeChanged();
  }

 private:
  const gfx::Insets insets_;
  const int spacing_;
};

BEGIN_METADATA(Column)
END_METADATA

// A section's card: its fill rounded 15 and its 1-point border just inside
// the edge, drawn over its rows, which sit inside it.
class SectionCard : public Column {
  METADATA_HEADER(SectionCard, Column)

 public:
  SectionCard() : Column(gfx::Insets(kCardBorder)) {}

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kPaneColorSheetCard));
    // In pieces, so a tile inside the card rasters no path (ADR 0123).
    FillSquircle(*canvas, gfx::RectF(GetLocalBounds()), radius::kLg, flags);
  }

  void PaintChildren(const views::PaintInfo& info) override {
    Column::PaintChildren(info);
    // The border over the rows, as SwiftUI's overlay draws it, so a rule's
    // ends meet it.
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    ui::PaintRecorder recorder(info.context(), info.paint_recording_size(),
                               info.paint_recording_scale_x(),
                               info.paint_recording_scale_y(), nullptr);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(kCardBorder);
    flags.setColor(colors->GetColor(kPaneColorBorder));
    StrokeSquircleBorder(*recorder.canvas(), gfx::RectF(GetLocalBounds()),
                         radius::kLg, kCardBorder, flags);
  }
};

BEGIN_METADATA(SectionCard)
END_METADATA

// A part of the proxy's address, with no fill or border of its own: the
// field's are the address's. Its text as a Field's.
class CellField : public views::Textfield {
  METADATA_HEADER(CellField, views::Textfield)

 public:
  CellField() {
    SetBackgroundEnabled(false);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, kCellPadding)));
    views::FocusRing::Remove(this);
    SetFontList(SystemFont(kFieldText.size, kFieldText.weight));
    SetTextColorId(kPaneColorForeground);
    SetPlaceholderTextColorId(kPaneColorMutedForeground);
    // Grayscale rather than subpixel text, as a Field's (ADR 0060).
    GetRenderText()->set_subpixel_rendering_suppressed(true);
  }

 private:
  SelectionFollowsWindow selection_{this};
};

BEGIN_METADATA(CellField)
END_METADATA

}  // namespace

// A 1-point line across, in the border's color: the header's and footer's
// rules, and those between a card's rows.
class ProfileSheet::Hairline : public views::View {
  METADATA_HEADER(Hairline, views::View)

 public:
  Hairline() {
    SetBackground(views::CreateSolidBackground(kPaneColorBorder));
    GetViewAccessibility().SetIsIgnored(true);
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, 1);
  }
};

BEGIN_METADATA(ProfileSheet, Hairline)
END_METADATA

// What a switch shows or hides, or the warning: shown, it is as tall as its
// content, and while it comes or goes, a share of that, its content clipped
// to it and as faded (Motion.toggle), so what follows moves with it.
class ProfileSheet::Reveal : public views::View {
  METADATA_HEADER(Reveal, views::View)

 public:
  explicit Reveal(std::unique_ptr<views::View> content)
      : share_(0, base::BindRepeating(&Reveal::Moved, base::Unretained(this))) {
    content_ = AddChildView(std::move(content));
    SetVisible(false);
  }

  // At once without `compositor`, or less motion asked for.
  void SetShown(bool shown, ui::Compositor* compositor) {
    if (shown == (share_.target() > 0)) {
      return;
    }
    SetVisible(true);
    share_.MoveTo(shown ? 1 : 0, motion::Toggle(), compositor);
    // At once, it settled in the call.
    if (!share_.is_moving()) {
      Moved(share_.value());
    }
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int width = available_size.width().is_bounded()
                          ? available_size.width().value()
                          : ProfileSheet::kWidth;
    return gfx::Size(
        width, std::round(content_->GetHeightForWidth(width) * share_.value()));
  }

  void Layout(PassKey) override {
    content_->SetBounds(0, 0, width(), content_->GetHeightForWidth(width()));
  }

  void ChildPreferredSizeChanged(views::View* child) override {
    PreferredSizeChanged();
  }

 private:
  void Moved(double share) {
    const bool moving = share_.is_moving();
    if (moving && !layer()) {
      // Clipped to the share as it comes or goes, and as faded.
      SetPaintToLayer();
      layer()->SetFillsBoundsOpaquely(false);
      layer()->SetMasksToBounds(true);
    }
    if (layer()) {
      layer()->SetOpacity(static_cast<float>(share));
    }
    if (!moving) {
      DestroyLayer();
      SetVisible(share > 0);
    }
    PreferredSizeChanged();
  }

  raw_ptr<views::View> content_ = nullptr;
  ValueMotion share_;
};

BEGIN_METADATA(ProfileSheet, Reveal)
END_METADATA

// A row's title over its note, muted (the Swift's RowLabels): the title at 13
// on 18-point lines, the note at 12 on 16-point ones, 2 apart, each wrapping
// where the row leaves too little room; a note that changes crossfades
// (Motion.status).
class ProfileSheet::RowLabels : public views::View {
  METADATA_HEADER(RowLabels, views::View)

 public:
  RowLabels(const std::u16string& title, gfx::Insets insets)
      : insets_(insets),
        fade_(1,
              base::BindRepeating(&RowLabels::Faded, base::Unretained(this))) {
    title_ = AddChildView(
        Text(title, text::kRowTitle, kPaneColorForeground, /*wraps=*/true));
    for (raw_ptr<views::Label>* label : {&note_, &outgoing_}) {
      *label = AddChildView(Text(std::u16string(), text::kRowDescription,
                                 kPaneColorMutedForeground, /*wraps=*/true));
    }
    outgoing_->SetVisible(false);
    outgoing_->GetViewAccessibility().SetIsIgnored(true);
  }

  void SetNote(const std::u16string& note, bool crossfade) {
    if (note_->GetText() == note) {
      return;
    }
    const std::u16string old(note_->GetText());
    note_->SetText(note);
    PreferredSizeChanged();
    ui::Compositor* compositor =
        GetWidget() && IsDrawn() ? GetWidget()->GetCompositor() : nullptr;
    if (!crossfade || !compositor || old.empty() || motion::IsReduced()) {
      return;
    }
    // The old note fades out as the new one fades in, over each other.
    outgoing_->SetText(old);
    outgoing_->SetVisible(true);
    for (views::Label* label : {note_.get(), outgoing_.get()}) {
      if (!label->layer()) {
        label->SetPaintToLayer();
        label->layer()->SetFillsBoundsOpaquely(false);
      }
    }
    fade_.MoveTo(0, motion::Status(), nullptr);
    fade_.MoveTo(1, motion::Status(), compositor);
  }

  int HeightFor(int width) const {
    const int inner = std::max(width - insets_.width(), 0);
    return insets_.height() + title_->GetHeightForWidth(inner) +
           (note_->GetText().empty()
                ? 0
                : kNoteSpacing + note_->GetHeightForWidth(inner));
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    if (available_size.width().is_bounded()) {
      const int width = available_size.width().value();
      return gfx::Size(width, HeightFor(width));
    }
    const int width = std::max(title_->GetPreferredSize().width(),
                               note_->GetPreferredSize().width()) +
                      insets_.width();
    return gfx::Size(width, HeightFor(width));
  }

  void Layout(PassKey) override {
    const int inner = std::max(width() - insets_.width(), 0);
    const int title = title_->GetHeightForWidth(inner);
    // In the row's middle, as an HStack centers its parts.
    const int top = insets_.top() + (height() - HeightFor(width())) / 2;
    title_->SetBounds(insets_.left(), top, inner, title);
    for (views::Label* label : {note_.get(), outgoing_.get()}) {
      label->SetBounds(insets_.left(), top + title + kNoteSpacing, inner,
                       label->GetHeightForWidth(inner));
    }
  }

 private:
  void Faded(double shown) {
    if (!note_->layer() || !outgoing_->layer()) {
      return;
    }
    note_->layer()->SetOpacity(shown);
    outgoing_->layer()->SetOpacity(1 - shown);
    if (shown >= 1 && !fade_.is_moving()) {
      outgoing_->SetVisible(false);
      note_->DestroyLayer();
      outgoing_->DestroyLayer();
    }
  }

  const gfx::Insets insets_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> note_ = nullptr;
  raw_ptr<views::Label> outgoing_ = nullptr;
  ValueMotion fade_;
};

BEGIN_METADATA(ProfileSheet, RowLabels)
END_METADATA

// A swatch: the color at 24 points, rounded 11, and the chosen one ringed
// with the card, then its color, concentric with it (DESIGN.md › Shape). A
// press chooses it at once.
class ProfileSheet::Swatch : public views::Button,
                             public views::ViewTargeterDelegate {
  METADATA_HEADER(Swatch, views::Button)

 public:
  Swatch(model::ProfileColor color, PressedCallback pressed)
      : views::Button(std::move(pressed)), color_(color) {
    SetID(kViewIdProfileSwatch);
    // The swatch alone takes the press, as the Swift's did, not its ring's
    // room.
    SetEventTargeter(std::make_unique<views::ViewTargeter>(this));
    GetViewAccessibility().SetName(ColorTitle(color));
    FocusRing::Install(this, ControlShape::Squircle(radius::kMd))
        ->SetAround(base::BindRepeating([](const views::View* view) {
          return gfx::Rect(kSwatchRing, kSwatchRing, kSwatch, kSwatch);
        }));
  }

  model::ProfileColor color() const { return color_; }

  void SetChosen(bool chosen) {
    if (chosen_ == chosen) {
      return;
    }
    chosen_ = chosen;
    GetViewAccessibility().SetIsSelected(chosen);
    SchedulePaint();
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kSwatch + 2 * kSwatchRing, kSwatch + 2 * kSwatchRing);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const gfx::RectF swatch(kSwatchRing, kSwatchRing, kSwatch, kSwatch);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (chosen_) {
      // The ring in its color 3 out, the card's 2 out, at the radius grown
      // by as much.
      gfx::RectF outer = swatch;
      outer.Outset(kSwatchRing);
      flags.setColor(ProfileColorOf(color_));
      canvas->DrawPath(SquirclePath(outer, radius::kMd + kSwatchRing), flags);
      gfx::RectF card = swatch;
      card.Outset(kSwatchRing - 1);
      flags.setColor(colors->GetColor(kPaneColorCard));
      canvas->DrawPath(SquirclePath(card, radius::kMd + kSwatchRing - 1),
                       flags);
    }
    flags.setColor(ProfileColorOf(color_));
    canvas->DrawPath(SquirclePath(swatch, radius::kMd), flags);
  }

  // views::ViewTargeterDelegate:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override {
    return gfx::Rect(kSwatchRing, kSwatchRing, kSwatch, kSwatch)
        .Intersects(rect);
  }

 private:
  const model::ProfileColor color_;
  bool chosen_ = false;
};

BEGIN_METADATA(ProfileSheet, Swatch)
END_METADATA

// A platform: its real logo over its name, on a tile rounded 11, the chosen
// one filled and ringed in the foreground, 2 points, as the App icon
// picker's (ADR 0089). Hover fills the others; choosing is instant. Its
// tile's edges sit on the screen's pixels where SwiftUI put them, a third of
// the row each: Views' whole points leave it a share of one.
class ProfileSheet::PlatformTile : public views::Button {
  METADATA_HEADER(PlatformTile, views::Button)

 public:
  PlatformTile(model::Platform platform, PressedCallback pressed)
      : views::Button(std::move(pressed)),
        platform_(platform),
        highlight_(this,
                   ControlShape::Squircle(radius::kMd),
                   0.5f,
                   base::BindRepeating(&PlatformTile::SchedulePaint,
                                       base::Unretained(this))) {
    SetID(kViewIdProfilePlatformTile);
    name_ = AddChildView(Text(base::UTF8ToUTF16(model::TitleOf(platform)),
                              kTileName, kPaneColorMutedLifted));
    name_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    name_->SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetName(base::UTF8ToUTF16(model::TitleOf(platform)));
  }

  model::Platform platform() const { return platform_; }

  void SetChosen(bool chosen) {
    if (chosen_ == chosen) {
      return;
    }
    chosen_ = chosen;
    name_->SetEnabledColor(chosen ? kPaneColorForeground
                                  : kPaneColorMutedLifted);
    GetViewAccessibility().SetIsSelected(chosen);
    // At once: a selection never animates.
    SchedulePaint();
  }

  // Where the tile is drawn in this view's bounds, on the screen's pixels:
  // painted anew, its layout the same (the row sets it as it lays out, and an
  // invalidated layout there would lay the window out again every frame).
  void SetTile(const gfx::RectF& tile) {
    if (tile_ == tile) {
      return;
    }
    tile_ = tile;
    SchedulePaint();
  }

  // views::Button:
  void Layout(PassKey) override {
    // The logo's box and the name's line, 4 apart, in the tile's middle.
    const int content = kLogoBox + kLogoSpacing + kTileName.line_height;
    const int top = (height() - content) / 2;
    name_->SetBounds(0, top + kLogoBox + kLogoSpacing, width(),
                     kTileName.line_height);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const gfx::RectF tile =
        tile_.IsEmpty() ? gfx::RectF(GetLocalBounds()) : tile_;
    const SkPath shape = SquirclePath(tile, radius::kMd);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (chosen_) {
      flags.setColor(colors->GetColor(kPaneColorChosenTile));
      canvas->DrawPath(shape, flags);
    } else if (highlight_.hover() > 0) {
      const SkColor hover = colors->GetColor(kPaneColorOutlineHover);
      flags.setColor(SkColorSetA(
          hover, static_cast<U8CPU>(SkColorGetA(hover) * highlight_.hover())));
      canvas->DrawPath(shape, flags);
    }
    if (highlight_.press() > 0) {
      const SkColor press = colors->GetColor(kPaneColorPress);
      flags.setColor(SkColorSetA(
          press, static_cast<U8CPU>(SkColorGetA(press) * highlight_.press())));
      canvas->DrawPath(shape, flags);
    }
    // Its edge just inside: the foreground's 2 points when chosen.
    const float edge = chosen_ ? 2 : 1;
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(edge);
    flags.setColor(
        colors->GetColor(chosen_ ? kPaneColorForeground : kPaneColorBorder));
    canvas->DrawPath(SquircleStrokePath(tile, radius::kMd, edge), flags);

    const int content = kLogoBox + kLogoSpacing + kTileName.line_height;
    const float top = (height() - content) / 2.0f;
    const gfx::SizeF logo = PlatformLogoSize(platform_);
    PaintPlatformLogo(*canvas, *colors, platform_,
                      gfx::PointF(tile.CenterPoint().x() - logo.width() / 2,
                                  top + (kLogoBox - logo.height()) / 2));
  }

 private:
  const model::Platform platform_;
  bool chosen_ = false;
  gfx::RectF tile_;
  raw_ptr<views::Label> name_ = nullptr;
  ControlHighlight highlight_;
};

BEGIN_METADATA(ProfileSheet, PlatformTile)
END_METADATA

// The three tiles across the row, 8 apart, a third of what is left each,
// their edges on the screen's pixels as SwiftUI put them.
class ProfileSheet::TileRow : public views::View {
  METADATA_HEADER(TileRow, views::View)

 public:
  TileRow() {
    SetID(kViewIdProfilePlatform);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(u"Platform");
  }

  PlatformTile* AddTile(std::unique_ptr<PlatformTile> tile) {
    return AddChildView(std::move(tile));
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, kTileHeight);
  }

  void Layout(PassKey) override {
    const float scale = ScaleOf(GetWidget());
    const float count = children().size();
    const float each = (width() - kTileSpacing * (count - 1)) / count;
    for (size_t index = 0; index < children().size(); ++index) {
      const float at = index * (each + kTileSpacing);
      const float start = std::round(at * scale) / scale;
      const float end = std::round((at + each) * scale) / scale;
      const int left = std::floor(start);
      const int right = std::ceil(end);
      auto* tile = static_cast<PlatformTile*>(children()[index]);
      tile->SetBounds(left, 0, right - left, kTileHeight);
      // Where it is drawn, in its own bounds.
      tile->SetTile(gfx::RectF(start - left, 0, end - start, kTileHeight));
    }
  }

  void AddedToWidget() override { InvalidateLayout(); }
};

BEGIN_METADATA(ProfileSheet, TileRow)
END_METADATA

// The proxy's address as one field (the Swift's ProxyAddressField): its type,
// host, and port, apart by rules, on the card's field fill in the field's
// border, its corners rounded 11, the focus ring around the whole while
// either part has the keys. What keeps the host or the port from saving rings
// that part alone, in red (Paper 05.8, 05.9, and 05.11): the host's cell, over
// its rules, or the field's own right end and the rule before the port.
class ProfileSheet::AddressField : public views::View,
                                   public views::ViewObserver {
  METADATA_HEADER(AddressField, views::View)

 public:
  enum class Part { kNone, kHost, kPort };

  AddressField(base::RepeatingCallback<void(size_t)> type_chosen)
      : ringed_shown_(
            0,
            base::BindRepeating(
                [](AddressField* field, double) { field->SchedulePaint(); },
                base::Unretained(this))) {
    std::vector<MenuButton::Option> types;
    for (const model::ProxyType type : kProxyTypes) {
      types.push_back({.title = ProxyTypeTitle(type)});
    }
    type_ = AddChildView(std::make_unique<MenuButton>(
        u"Proxy type", std::move(types), std::move(type_chosen),
        MenuButton::Style::kEmbedded));
    type_->SetID(kViewIdProfileProxyType);
    for (raw_ptr<views::Textfield>* field : {&host_, &port_}) {
      *field = AddChildView(std::make_unique<CellField>());
      (*field)->AddObserver(this);
    }
    host_->SetID(kViewIdProfileProxyHost);
    host_->SetPlaceholderText(u"Host");
    host_->GetViewAccessibility().SetName(u"Proxy host");
    port_->SetID(kViewIdProfileProxyPort);
    port_->SetPlaceholderText(u"Port");
    port_->GetViewAccessibility().SetName(u"Proxy port");
    ring_ = FocusRing::Install(this, ControlShape::Squircle(radius::kMd));
    ring_->SetFocusWithin(true);
    ring_->SetShowsWhen(base::BindRepeating([](const views::View* view) {
      return static_cast<const AddressField*>(view)->ringed_ == Part::kNone;
    }));
  }

  ~AddressField() override {
    host_->RemoveObserver(this);
    port_->RemoveObserver(this);
  }

  MenuButton* type() { return type_; }
  views::Textfield* host() { return host_; }
  views::Textfield* port() { return port_; }

  // The part a problem rings, and its words for the screen reader.
  void SetRinged(Part part, const std::u16string& message) {
    host_->GetViewAccessibility().SetDescription(
        part == Part::kHost ? message : std::u16string());
    port_->GetViewAccessibility().SetDescription(
        part == Part::kPort ? message : std::u16string());
    if (part == ringed_) {
      return;
    }
    // A ring going fades from the part it rang.
    if (part != Part::kNone) {
      fading_ = part;
    }
    ringed_ = part;
    ringed_shown_.MoveTo(
        part == Part::kNone ? 0 : 1, motion::Toggle(),
        GetWidget() && IsDrawn() ? GetWidget()->GetCompositor() : nullptr);
    ring_->Refresh();
    SchedulePaint();
  }

  // A part's focus changed: the host's leaving is a paste's chance.
  void SetOnHostLeft(base::RepeatingClosure host_left) {
    host_left_ = std::move(host_left);
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, Field::kHeight);
  }

  void Layout(PassKey) override {
    const int type = type_->GetPreferredSize().width();
    type_->SetBounds(0, 0, type, height());
    const int host_start = type + 1;
    const int port_start = width() - kPortWidth;
    host_->SetBounds(host_start, 0, std::max(port_start - 1 - host_start, 0),
                     height());
    port_->SetBounds(port_start, 0, kPortWidth, height());
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kPaneColorSheetCardField));
    canvas->DrawPath(SquirclePath(bounds, radius::kMd), flags);
    // The rules between the parts, the field's height, under its border.
    const SkColor border = colors->GetColor(kPaneColorBorder);
    canvas->FillRect(gfx::Rect(type_->bounds().right(), 0, 1, height()),
                     border);
    canvas->FillRect(gfx::Rect(port_->x() - 1, 0, 1, height()), border);
  }

  void PaintChildren(const views::PaintInfo& info) override {
    // The parts clipped to the field's corners, as the Swift's clipShape: the
    // type's hover is a rectangle. The recording is in the screen's pixels,
    // as View::Paint's own clip is.
    {
      ui::ClipRecorder clip(info.context());
      clip.ClipPathWithAntiAliasing(
          SquirclePath(gfx::RectF(GetLocalBounds()), radius::kMd)
              .makeScale(info.paint_recording_scale_x(),
                         info.paint_recording_scale_y()));
      views::View::PaintChildren(info);
    }
    ui::PaintRecorder recorder(info.context(), info.paint_recording_size(),
                               info.paint_recording_scale_x(),
                               info.paint_recording_scale_y(), nullptr);
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    gfx::Canvas* canvas = recorder.canvas();
    const gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(colors->GetColor(kPaneColorBorder));
    canvas->DrawPath(SquircleStrokePath(bounds, radius::kMd, 1), flags);
    // A part's ring over the border and the rules, in their place around it.
    const double shown = ringed_shown_.value();
    const Part part = ringed_ != Part::kNone ? ringed_ : fading_;
    if (shown <= 0 || part == Part::kNone) {
      return;
    }
    const SkColor red = colors->GetColor(kPaneColorDestructive);
    flags.setColor(
        SkColorSetA(red, static_cast<U8CPU>(SkColorGetA(red) * shown)));
    if (part == Part::kHost) {
      // The host's cell, a point over each rule, the field's height.
      gfx::RectF cell(host_->bounds());
      cell.Outset(gfx::OutsetsF::VH(0, 1));
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      cell.Inset(0.5f);
      canvas->DrawRect(cell, flags);
      return;
    }
    // The field's own right end, from the rule before the port, and a line in
    // that rule's place.
    {
      gfx::ScopedCanvas scoped(canvas);
      canvas->ClipRect(
          gfx::RectF(port_->x() - 1, 0, width() - port_->x() + 1, height()));
      canvas->DrawPath(SquircleStrokePath(bounds, radius::kMd, 1), flags);
    }
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRect(gfx::RectF(port_->x() - 1, 0, 1, height()), flags);
  }

  // views::ViewObserver: the ring follows the parts' focus.
  void OnViewFocused(views::View* view) override { ring_->Refresh(); }
  void OnViewBlurred(views::View* view) override {
    ring_->Refresh();
    if (view == host_ && host_left_) {
      host_left_.Run();
    }
  }

 private:
  raw_ptr<MenuButton> type_ = nullptr;
  raw_ptr<views::Textfield> host_ = nullptr;
  raw_ptr<views::Textfield> port_ = nullptr;
  raw_ptr<FocusRing> ring_ = nullptr;
  Part ringed_ = Part::kNone;
  // The part a ring last showed around, while it fades.
  Part fading_ = Part::kNone;
  ValueMotion ringed_shown_;
  base::RepeatingClosure host_left_;
};

BEGIN_METADATA(ProfileSheet, AddressField)
END_METADATA

// The proxy test's answer beside its button (Paper 05.5): a dot and its
// words, yellow while it runs, the proxy's IP on emerald, the error on red,
// read as one by the screen reader.
class ProfileSheet::TestStatus : public views::View {
  METADATA_HEADER(TestStatus, views::View)

 public:
  TestStatus() {
    SetID(kViewIdProfileProxyStatus);
    text_ = AddChildView(Text(std::u16string(), kNote, kPaneColorForeground,
                              /*wraps=*/true));
    text_->GetViewAccessibility().SetIsIgnored(true);
    GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
  }

  void Set(TestState state, const std::u16string& text) {
    if (state_ == state && text_->GetText() == text) {
      return;
    }
    state_ = state;
    text_->SetText(text);
    GetViewAccessibility().SetName(text);
    SetVisible(state != TestState::kNone);
    PreferredSizeChanged();
    SchedulePaint();
  }

  int HeightFor(int width) const {
    return text_->GetHeightForWidth(std::max(width - kDot - kDotSpacing, 0));
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    if (available_size.width().is_bounded()) {
      return gfx::Size(available_size.width().value(),
                       HeightFor(available_size.width().value()));
    }
    const gfx::Size text = text_->GetPreferredSize();
    return gfx::Size(kDot + kDotSpacing + text.width(), text.height());
  }

  void Layout(PassKey) override {
    const int text_width = std::max(width() - kDot - kDotSpacing, 0);
    text_->SetBounds(kDot + kDotSpacing, 0, text_width,
                     text_->GetHeightForWidth(text_width));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors || state_ == TestState::kNone) {
      return;
    }
    const ui::ColorId dot = state_ == TestState::kTesting ? kPaneColorWarning
                            : state_ == TestState::kConnected
                                ? kPaneColorSuccess
                                : kPaneColorFailure;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(dot));
    // On the first line's middle, as an HStack centers it on one line.
    canvas->DrawCircle(gfx::PointF(kDot / 2.0f, kNote.line_height / 2.0f),
                       kDot / 2.0f, flags);
  }

 private:
  TestState state_ = TestState::kNone;
  raw_ptr<views::Label> text_ = nullptr;
};

BEGIN_METADATA(ProfileSheet, TestStatus)
END_METADATA

namespace {

// A label over what it names: a form's (12 medium on a 16-point line, 8
// over its field), or a section's (13, muted, 15 in, 8 over its card).
std::unique_ptr<Column> Labeled(std::unique_ptr<views::Label> label,
                                int label_inset,
                                int spacing,
                                std::unique_ptr<views::View> content) {
  auto column = std::make_unique<Column>(gfx::Insets(), spacing);
  label->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(0, label_inset, 0, 0)));
  label->GetViewAccessibility().SetIsIgnored(true);
  column->AddChildView(std::move(label));
  column->AddChildView(std::move(content));
  return column;
}

// A switch's row: the switch with its labels across the row, but for the
// card's 14 beside the track, outside its button.
class SwitchRow : public views::View {
  METADATA_HEADER(SwitchRow, views::View)

 public:
  explicit SwitchRow(std::unique_ptr<Switch> control) {
    switch_ = AddChildView(std::move(control));
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int width = available_size.width().is_bounded()
                          ? available_size.width().value()
                          : ProfileSheet::kWidth;
    return gfx::Size(width, switch_->GetHeightForWidth(
                                std::max(width - kSwitchTrailing, 0)));
  }

  void Layout(PassKey) override {
    switch_->SetBounds(0, 0, std::max(width() - kSwitchTrailing, 0), height());
  }

  void ChildPreferredSizeChanged(views::View* child) override {
    PreferredSizeChanged();
  }

 private:
  raw_ptr<Switch> switch_ = nullptr;
};

BEGIN_METADATA(SwitchRow)
END_METADATA

// Views side by side across the row, `spacing` apart: `flexible` ones share
// what the others leave, each as wide as it needs otherwise; each in the
// row's middle, as an HStack centers them.
class Row : public views::View {
  METADATA_HEADER(Row, views::View)

 public:
  explicit Row(int spacing) : spacing_(spacing) {}

  template <typename T>
  T* Add(std::unique_ptr<T> view, bool flexible) {
    T* added = AddChildView(std::move(view));
    flexible_.push_back(flexible);
    return added;
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int width = available_size.width().is_bounded()
                          ? available_size.width().value()
                          : ProfileSheet::kWidth;
    const std::vector<int> widths = Widths(width);
    int height = 0;
    for (size_t index = 0; index < children().size(); ++index) {
      if (children()[index]->GetVisible()) {
        height = std::max(height,
                          children()[index]->GetHeightForWidth(widths[index]));
      }
    }
    return gfx::Size(width, height);
  }

  void Layout(PassKey) override {
    const std::vector<int> widths = Widths(width());
    int x = 0;
    for (size_t index = 0; index < children().size(); ++index) {
      views::View* child = children()[index];
      if (!child->GetVisible()) {
        continue;
      }
      const int height =
          std::min(child->GetHeightForWidth(widths[index]), this->height());
      child->SetBounds(x, (this->height() - height) / 2, widths[index], height);
      x += widths[index] + spacing_;
    }
  }

  void ChildPreferredSizeChanged(views::View* child) override {
    PreferredSizeChanged();
  }

 private:
  std::vector<int> Widths(int width) const {
    std::vector<int> widths(children().size(), 0);
    int fixed = 0;
    int flexible = 0;
    int shown = 0;
    for (size_t index = 0; index < children().size(); ++index) {
      if (!children()[index]->GetVisible()) {
        continue;
      }
      ++shown;
      if (flexible_[index]) {
        ++flexible;
      } else {
        widths[index] = children()[index]->GetPreferredSize().width();
        fixed += widths[index];
      }
    }
    const int left =
        std::max(width - fixed - spacing_ * std::max(shown - 1, 0), 0);
    int given = 0;
    int seen = 0;
    for (size_t index = 0; index < children().size(); ++index) {
      if (!children()[index]->GetVisible() || !flexible_[index]) {
        continue;
      }
      ++seen;
      // The last takes what rounding left.
      widths[index] = seen == flexible ? left - given : left / flexible;
      given += widths[index];
    }
    return widths;
  }

  const int spacing_;
  std::vector<bool> flexible_;
};

BEGIN_METADATA(Row)
END_METADATA

// The swatches across, 8 apart, each in room for its ring.
class SwatchRow : public views::View {
  METADATA_HEADER(SwatchRow, views::View)

 public:
  SwatchRow() {
    SetID(kViewIdProfileColor);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(u"Color");
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, kSwatch + 2 * kSwatchRing);
  }

  void Layout(PassKey) override {
    int x = -kSwatchRing;
    for (views::View* swatch : children()) {
      swatch->SetBounds(x, 0, kSwatch + 2 * kSwatchRing,
                        kSwatch + 2 * kSwatchRing);
      x += kSwatch + kSwatchSpacing;
    }
  }
};

BEGIN_METADATA(SwatchRow)
END_METADATA

// The warning another platform than the computer's brings (Paper 05.5): its
// icon in the warning's color, 1 down, beside its words at 12 on 16-point
// lines, on the warning's fill rounded 9.
class Warning : public views::View {
  METADATA_HEADER(Warning, views::View)

 public:
  Warning() {
    SetID(kViewIdProfileWarning);
    text_ = AddChildView(
        Text(std::u16string(), kNote, kPaneColorAccentForeground, true));
  }

  views::Label* text() { return text_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int width = available_size.width().is_bounded()
                          ? available_size.width().value()
                          : ProfileSheet::kWidth;
    return gfx::Size(width, kWarningPadding.height() +
                                text_->GetHeightForWidth(TextWidth(width)));
  }

  void Layout(PassKey) override {
    const int text = TextWidth(width());
    text_->SetBounds(
        kWarningPadding.left() + kWarningIcon + kWarningIconSpacing,
        kWarningPadding.top(), text, text_->GetHeightForWidth(text));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kPaneColorWarningFill));
    canvas->DrawPath(SquirclePath(gfx::RectF(GetLocalBounds()), radius::kTab),
                     flags);
    PaintIcon(*canvas, Icon::kTriangleAlert,
              gfx::RectF(kWarningPadding.left(), kWarningPadding.top() + 1,
                         kWarningIcon, kWarningIcon),
              2, colors->GetColor(kPaneColorWarningIcon));
  }

 private:
  static int TextWidth(int width) {
    return std::max(
        width - kWarningPadding.width() - kWarningIcon - kWarningIconSpacing,
        0);
  }

  raw_ptr<views::Label> text_ = nullptr;
};

BEGIN_METADATA(Warning)
END_METADATA

// Test connection on the card's field fill, which stays whole while the
// button dims, as the Swift's background sat outside the button's opacity.
class TestButtonPlace : public views::View {
  METADATA_HEADER(TestButtonPlace, views::View)

 public:
  explicit TestButtonPlace(std::unique_ptr<TextButton> button) {
    button_ = AddChildView(std::move(button));
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return button_->GetPreferredSize();
  }

  void Layout(PassKey) override { button_->SetBoundsRect(GetLocalBounds()); }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kPaneColorSheetCardField));
    canvas->DrawPath(SquirclePath(gfx::RectF(GetLocalBounds()), radius::kTab),
                     flags);
  }

 private:
  raw_ptr<TextButton> button_ = nullptr;
};

BEGIN_METADATA(TestButtonPlace)
END_METADATA

// The credentials' two fields across, 8 apart, each half of what is left.
std::unique_ptr<Field> SheetField(ui::ColorVariant fill,
                                  const std::u16string& placeholder,
                                  const std::u16string& name,
                                  int id) {
  auto field = std::make_unique<Field>(fill, kFieldText);
  field->SetPlaceholderText(placeholder);
  field->GetViewAccessibility().SetName(name);
  field->SetID(id);
  return field;
}

}  // namespace

ProfileSheet::Callbacks::Callbacks() = default;
ProfileSheet::Callbacks::Callbacks(Callbacks&&) = default;
ProfileSheet::Callbacks& ProfileSheet::Callbacks::operator=(Callbacks&&) =
    default;
ProfileSheet::Callbacks::~Callbacks() = default;

ProfileSheet::ProfileSheet(Callbacks callbacks)
    : callbacks_(std::move(callbacks)),
      scroll_motion_(
          0,
          base::BindRepeating(
              [](ProfileSheet* sheet, double offset) {
                sheet->scroll_->ScrollToOffset(gfx::PointF(0, offset));
              },
              base::Unretained(this))) {
  SetID(kViewIdProfileSheet);
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  // VoiceOver stays in the sheet while it shows, as in Quit Pane?.
  GetViewAccessibility().SetIsModal(true);

  // The header: the title, and the close X, muted until hovered.
  title_ =
      AddChildView(Text(u"New profile", kTitle, kPaneColorAccentForeground));
  title_->GetViewAccessibility().SetRole(ax::mojom::Role::kHeading);
  ControlLook close_look = IconButtonLook(ControlShape::Squircle(radius::kSm));
  close_look.hover_fill.reset();
  close_look.lit_tint = kPaneColorForeground;
  close_ = AddChildView(std::make_unique<IconButton>(
      base::BindRepeating(&ProfileSheet::Cancel, base::Unretained(this)),
      kCloseSize, IconButton::Glyph{.icon = Icon::kX, .size = 16}, u"Close",
      close_look));
  close_->SetID(kViewIdProfileClose);
  header_rule_ = AddChildView(std::make_unique<Hairline>());

  // The body, ending hard at the two rules (DESIGN.md › Scroll edges).
  scroll_ = AddChildView(LayeredScrollView());
  scroll_->SetID(kViewIdProfileBody);
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->SetDrawOverflowIndicator(false);
  scroll_->SetUseContentsPreferredSize(true);
  // The body's contents (the Swift's VStack in its ScrollView): 20 in, the
  // name and the color, then each section's label over its card, 18 apart.
  contents_ = scroll_->SetContents(
      std::make_unique<Column>(gfx::Insets(kInset), kSectionSpacing));

  // Identity: the name, then the color.
  {
    auto identity = std::make_unique<Column>(gfx::Insets(), kIdentitySpacing);
    auto name = SheetField(kPaneColorWindowBackground, u"e.g. Work account",
                           u"Name", kViewIdProfileName);
    name_ = name.get();
    name_block_ = identity->AddChildView(
        Labeled(Text(u"Name", kFormLabel, kPaneColorForeground), 0,
                kLabelSpacing, std::move(name)));
    auto swatches = std::make_unique<SwatchRow>();
    for (const model::ProfileColor color : kColors) {
      swatches_.push_back(swatches->AddChildView(std::make_unique<Swatch>(
          color, base::BindRepeating(
                     [](ProfileSheet* sheet, model::ProfileColor color) {
                       sheet->draft_.color = color;
                       sheet->Edited();
                     },
                     base::Unretained(this), color))));
    }
    identity->AddChildView(
        Labeled(Text(u"Color", kFormLabel, kPaneColorForeground), 0,
                kLabelSpacing, std::move(swatches)));
    contents_->AddChildView(std::move(identity));
  }

  const auto section = [this](const std::u16string& title,
                              std::unique_ptr<SectionCard> card) {
    card->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    card->GetViewAccessibility().SetName(title);
    contents_->AddChildView(
        Labeled(Text(title, kSectionTitle, kPaneColorMutedForeground),
                kSectionTitleInset, kSectionTitleSpacing, std::move(card)));
  };

  const auto switch_row = [](const std::u16string& title, int id,
                             Switch::Toggled toggled, raw_ptr<Switch>& control,
                             raw_ptr<RowLabels>& labels) {
    auto row_labels = std::make_unique<RowLabels>(title, kSwitchLabels);
    labels = row_labels.get();
    auto made = std::make_unique<Switch>(std::move(toggled),
                                         std::move(row_labels), title);
    made->SetID(id);
    control = made.get();
    return std::make_unique<SwitchRow>(std::move(made));
  };

  // Privacy: Save history.
  {
    auto card = std::make_unique<SectionCard>();
    card->AddChildView(switch_row(u"Save history", kViewIdProfileHistory,
                                  base::BindRepeating(
                                      [](ProfileSheet* sheet, bool on) {
                                        sheet->draft_.saves_history = on;
                                        sheet->Edited();
                                      },
                                      base::Unretained(this)),
                                  history_, history_labels_));
    section(u"Privacy", std::move(card));
  }

  // Fingerprint: Spoof fingerprint, then, while it is on, the platforms and
  // the zone.
  {
    auto card = std::make_unique<SectionCard>();
    card->AddChildView(switch_row(u"Spoof fingerprint", kViewIdProfileSpoof,
                                  base::BindRepeating(
                                      [](ProfileSheet* sheet, bool on) {
                                        sheet->draft_.spoofing = on;
                                        sheet->Edited();
                                      },
                                      base::Unretained(this)),
                                  spoof_, spoof_labels_));

    auto rows = std::make_unique<Column>();
    rows->AddChildView(std::make_unique<Hairline>());
    auto platform = std::make_unique<Column>(kRowPadding);
    auto tiles = std::make_unique<TileRow>();
    for (const model::Platform each : kPlatforms) {
      tiles_.push_back(tiles->AddTile(std::make_unique<PlatformTile>(
          each, base::BindRepeating(
                    [](ProfileSheet* sheet, model::Platform platform) {
                      if (sheet->GetWidget()) {
                        LogInteraction(Interaction::kChoosePlatform,
                                       sheet->GetWidget()->GetCompositor());
                      }
                      sheet->draft_.platform = platform;
                      sheet->Edited();
                    },
                    base::Unretained(this), each))));
    }
    platform->AddChildView(std::move(tiles));
    auto warning = std::make_unique<Warning>();
    warning_text_ = warning->text();
    auto warning_place =
        std::make_unique<Column>(gfx::Insets::TLBR(kWarningSpacing, 0, 0, 0));
    warning_place->AddChildView(std::move(warning));
    warning_ = platform->AddChildView(
        std::make_unique<Reveal>(std::move(warning_place)));
    rows->AddChildView(std::move(platform));
    rows->AddChildView(std::make_unique<Hairline>());

    auto zone_row = std::make_unique<Column>(kRowPadding);
    auto zone = std::make_unique<Row>(kZoneSpacing);
    zone_labels_ = zone->Add(
        std::make_unique<RowLabels>(u"Time zone", gfx::Insets()), true);
    // Read as the menu's own name and description, as a switch reads its
    // row's labels: VoiceOver read the title, then the menu by the same name.
    zone_labels_->GetViewAccessibility().SetIsLeaf(true);
    zone_labels_->GetViewAccessibility().SetIsIgnored(true);
    zone_ =
        zone->Add(std::make_unique<MenuButton>(
                      u"Time zone",
                      std::vector<MenuButton::Option>{{.title = u"Automatic"}},
                      base::BindRepeating(&ProfileSheet::ChooseZone,
                                          base::Unretained(this))),
                  false);
    zone_->SetID(kViewIdProfileTimeZone);
    // From the press to the system's menu about to show, as Settings' menus
    // are timed.
    MenuButton::Opening opening;
    opening.asked = base::BindRepeating(
        [](ProfileSheet* sheet) {
          sheet->opening_menu_ =
              BeginInteractionInterval(Interaction::kOpenMenu);
        },
        base::Unretained(this));
    opening.shows = base::BindRepeating(
        [](ProfileSheet* sheet) {
          if (const std::optional<uint64_t> interval =
                  std::exchange(sheet->opening_menu_, std::nullopt)) {
            EndInteractionInterval(Interaction::kOpenMenu, *interval);
          }
        },
        base::Unretained(this));
    zone_->SetOpening(std::move(opening));
    zone_row->AddChildView(std::move(zone));
    rows->AddChildView(std::move(zone_row));
    spoof_rows_ = card->AddChildView(std::make_unique<Reveal>(std::move(rows)));
    section(u"Fingerprint", std::move(card));
  }

  // Network: Proxy, then, while it is on, its address, credentials, and test.
  {
    auto card = std::make_unique<SectionCard>();
    card->AddChildView(switch_row(u"Proxy", kViewIdProfileProxy,
                                  base::BindRepeating(
                                      [](ProfileSheet* sheet, bool on) {
                                        sheet->draft_.proxy_enabled = on;
                                        sheet->Edited();
                                      },
                                      base::Unretained(this)),
                                  proxy_, proxy_labels_));

    auto rows = std::make_unique<Column>();
    rows->AddChildView(std::make_unique<Hairline>());
    auto fields = std::make_unique<Column>(kProxyPadding, kProxySpacing);
    address_ =
        fields->AddChildView(std::make_unique<AddressField>(base::BindRepeating(
            [](ProfileSheet* sheet, size_t index) {
              sheet->draft_.proxy_type = kProxyTypes[index];
              sheet->Edited();
            },
            base::Unretained(this))));
    address_->SetOnHostLeft(base::BindRepeating(
        [](ProfileSheet* sheet) {
          if (sheet->draft_.PasteProxy(sheet->draft_.proxy_host)) {
            sheet->Edited();
          }
        },
        base::Unretained(this)));
    auto credentials = std::make_unique<Row>(kProxySpacing);
    auto username = SheetField(kPaneColorSheetCardField, u"Username (optional)",
                               u"Proxy username", kViewIdProfileProxyUsername);
    username_ = credentials->Add(std::move(username), true);
    auto password = SheetField(kPaneColorSheetCardField, u"Password (optional)",
                               u"Proxy password", kViewIdProfileProxyPassword);
    password->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
    password_ = credentials->Add(std::move(password), true);
    fields->AddChildView(std::move(credentials));
    fields->AddChildView(
        Text(u"Paste host:port:user:password to fill them all.", kNote,
             kPaneColorMutedForeground));
    auto test = std::make_unique<Row>(kTestSpacing);
    ControlLook test_look = ButtonLook(ButtonVariant::kOutline, radius::kTab);
    auto test_button = std::make_unique<TextButton>(
        base::BindRepeating(&ProfileSheet::TestProxy, base::Unretained(this)),
        u"Test connection", test_look, button_size::kCompact);
    test_button->SetID(kViewIdProfileTestProxy);
    test_button_ = test_button.get();
    test->Add(std::make_unique<TestButtonPlace>(std::move(test_button)), false);
    test_status_ = test->Add(std::make_unique<TestStatus>(), true);
    auto test_place =
        std::make_unique<Column>(gfx::Insets::TLBR(kTestTop, 0, 0, 0));
    test_place->AddChildView(std::move(test));
    fields->AddChildView(std::move(test_place));
    rows->AddChildView(std::move(fields));
    proxy_rows_ = card->AddChildView(std::make_unique<Reveal>(std::move(rows)));
    section(u"Network", std::move(card));
  }

  // The footer: Cancel, then Create.
  footer_rule_ = AddChildView(std::make_unique<Hairline>());
  cancel_ = AddChildView(std::make_unique<TextButton>(
      base::BindRepeating(&ProfileSheet::Cancel, base::Unretained(this)),
      u"Cancel", ButtonVariant::kOutline));
  cancel_->SetID(kViewIdProfileCancel);
  save_ = AddChildView(std::make_unique<TextButton>(
      base::BindRepeating(&ProfileSheet::Submit, base::Unretained(this)),
      u"Create", ButtonVariant::kPrimary));
  save_->SetID(kViewIdProfileSave);

  // Each field's text is the draft's, as typed, pasted, or set by a screen
  // reader.
  const auto follow = [this](views::Textfield* field,
                             std::string model::ProfileDraft::* value) {
    text_subscriptions_.push_back(
        field->AddTextChangedCallback(base::BindRepeating(
            [](ProfileSheet* sheet, views::Textfield* field,
               std::string model::ProfileDraft::* value) {
              if (sheet->showing_) {
                return;
              }
              const std::string text = base::UTF16ToUTF8(field->GetText());
              // A paste into the host fills the whole address: a string
              // grown by more than a key's character at once.
              const bool pasted =
                  field == sheet->address_->host() &&
                  text.size() > (sheet->draft_.*value).size() + 1;
              sheet->draft_.*value = text;
              if (pasted) {
                sheet->draft_.PasteProxy(text);
              }
              sheet->Edited();
            },
            base::Unretained(this), field, value)));
  };
  follow(name_, &model::ProfileDraft::name);
  follow(address_->host(), &model::ProfileDraft::proxy_host);
  follow(address_->port(), &model::ProfileDraft::proxy_port);
  follow(username_, &model::ProfileDraft::proxy_username);
  follow(password_, &model::ProfileDraft::proxy_password);

  Begin(nullptr);
}

ProfileSheet::~ProfileSheet() = default;

void ProfileSheet::Begin(const model::BrowserProfile* profile) {
  test_.reset();
  testing_interval_.reset();
  draft_ = profile ? model::ProfileDraft(*profile) : model::ProfileDraft();
  profile_id_ = profile ? std::make_optional(profile->id) : std::nullopt;
  saved_history_ = profile && profile->saves_history;
  problem_.reset();
  test_state_ = TestState::kNone;
  test_text_.clear();
  tested_.reset();
  scroll_motion_.MoveTo(0, motion::Reveal(), nullptr);
  scroll_->ScrollToOffset(gfx::PointF());

  // The zone shows at once, the menu's zones once listed, off the UI thread:
  // 429 zones cost the frame that answers the click 3 ms, and a Pane left
  // open has to show today's offsets.
  zone_ids_ = {std::string()};
  std::vector<MenuButton::Option> options = {{.title = u"Automatic"}};
  if (!draft_.time_zone.empty()) {
    zone_ids_.push_back(draft_.time_zone);
    options.push_back(
        {.title = base::UTF8ToUTF16(draft_.time_zone),
         .shown = base::UTF8ToUTF16(model::TimeZoneCity(draft_.time_zone))});
  }
  zone_->SetOptions(std::move(options));
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE},
      base::BindOnce([] { return model::TimeZoneRegions(); }),
      base::BindOnce(&ProfileSheet::ZonesListed, weak_factory_.GetWeakPtr()));

  // Each field's text as the profile has it, its caret at its end.
  showing_ = true;
  name_->SetText(base::UTF8ToUTF16(draft_.name));
  showing_ = false;
  Show(/*animate=*/false);
}

void ProfileSheet::FocusName() {
  name_->RequestFocus();
  name_->SelectAll(false);
}

void ProfileSheet::Cancel() {
  test_.reset();
  testing_interval_.reset();
  callbacks_.close.Run();
}

void ProfileSheet::Submit() {
  // A proxy string typed and never left reads now, as it would have.
  draft_.PasteProxy(draft_.proxy_host);
  problem_ = draft_.Validate();
  if (!problem_) {
    test_.reset();
    testing_interval_.reset();
    callbacks_.save.Run(draft_);
    callbacks_.close.Run();
    return;
  }
  Show(/*animate=*/true);
  RevealProblem();
  // No words on the screen: the screen reader hears why.
  GetViewAccessibility().AnnounceAlert(
      base::UTF8ToUTF16(model::MessageOf(*problem_)));
}

void ProfileSheet::Edited() {
  if (problem_) {
    problem_ = draft_.Remaining(*problem_);
  }
  // A test's answer is about the proxy it tested, not the one typed since.
  if (test_state_ != TestState::kNone && draft_.Proxy() != tested_) {
    test_.reset();
    testing_interval_.reset();
    test_state_ = TestState::kNone;
    test_text_.clear();
    tested_.reset();
  }
  Show(/*animate=*/true);
}

void ProfileSheet::Show(bool animate) {
  ui::Compositor* compositor = animate && GetWidget() && IsDrawn()
                                   ? GetWidget()->GetCompositor()
                                   : nullptr;
  const bool is_new = !profile_id_.has_value();
  static_cast<views::Label*>(title_.get())
      ->SetText(is_new ? u"New profile" : u"Edit profile");
  GetViewAccessibility().SetName(is_new ? u"New profile" : u"Edit profile");
  save_->SetTitle(is_new ? u"Create" : u"Save");

  for (Swatch* swatch : swatches_) {
    swatch->SetChosen(swatch->color() == draft_.color);
  }

  history_->SetOn(draft_.saves_history);
  history_labels_->SetNote(HistoryNote(), animate);
  history_->GetViewAccessibility().SetDescription(HistoryNote());

  spoof_->SetOn(draft_.spoofing);
  spoof_labels_->SetNote(SpoofNote(), animate);
  spoof_->GetViewAccessibility().SetDescription(SpoofNote());
  spoof_rows_->SetShown(draft_.spoofing, compositor);
  for (PlatformTile* tile : tiles_) {
    tile->SetChosen(tile->platform() == draft_.platform);
  }
  const bool cross_os = draft_.platform != model::HostPlatform();
  if (cross_os) {
    static_cast<views::Label*>(warning_text_.get())
        ->SetText(base::StrCat(
            {u"Strict checks like Cloudflare’s can spot a ",
             base::UTF8ToUTF16(model::TitleOf(draft_.platform)),
             u" fingerprint on a ", Host(), u". Choose ",
             base::UTF8ToUTF16(model::TitleOf(model::HostPlatform())),
             u" for those sites."}));
  }
  warning_->SetShown(cross_os, compositor);
  zone_labels_->SetNote(ZoneNote(), animate);
  zone_->GetViewAccessibility().SetDescription(ZoneNote());
  const auto zone = std::ranges::find(zone_ids_, draft_.time_zone);
  zone_->SetSelected(zone == zone_ids_.end() ? std::nullopt
                                             : std::make_optional<size_t>(
                                                   zone - zone_ids_.begin()));

  proxy_->SetOn(draft_.proxy_enabled);
  const std::u16string proxy_note =
      draft_.proxy_enabled ? u"Its traffic goes through this proxy."
                           : u"Connects directly.";
  proxy_labels_->SetNote(proxy_note, animate);
  proxy_->GetViewAccessibility().SetDescription(proxy_note);
  proxy_rows_->SetShown(draft_.proxy_enabled, compositor);
  address_->type()->SetSelected(static_cast<size_t>(
      std::ranges::find(kProxyTypes, draft_.proxy_type) - kProxyTypes.begin()));

  // A field shows the draft's text once that is another than it has: a
  // paste, or the form begun.
  showing_ = true;
  const auto show_text = [](views::Textfield* field, const std::string& text) {
    const std::u16string value = base::UTF8ToUTF16(text);
    if (field->GetText() != value) {
      field->SetText(value);
    }
  };
  show_text(address_->host(), draft_.proxy_host);
  show_text(address_->port(), draft_.proxy_port);
  show_text(username_, draft_.proxy_username);
  show_text(password_, draft_.proxy_password);
  showing_ = false;

  // The problem's ring, its words for the screen reader alone.
  using Problem = model::ProfileDraft::Problem;
  name_->SetProblem(problem_ == Problem::kNameMissing
                        ? std::make_optional(base::UTF8ToUTF16(
                              model::MessageOf(Problem::kNameMissing)))
                        : std::nullopt);
  AddressField::Part ringed = AddressField::Part::kNone;
  if (problem_ == Problem::kHostMissing || problem_ == Problem::kHostInvalid) {
    ringed = AddressField::Part::kHost;
  } else if (problem_ == Problem::kPortInvalid) {
    ringed = AddressField::Part::kPort;
  }
  address_->SetRinged(ringed,
                      problem_ && ringed != AddressField::Part::kNone
                          ? base::UTF8ToUTF16(model::MessageOf(*problem_))
                          : std::u16string());

  test_button_->SetEnabled(test_state_ != TestState::kTesting &&
                           !model::TrimSpaces(draft_.proxy_host).empty());
  test_status_->Set(test_state_, test_text_);
}

void ProfileSheet::TestProxy() {
  // From the press to the frame showing the test's answer.
  testing_interval_ = BeginInteractionInterval(Interaction::kTestProxy);
  if (draft_.PasteProxy(draft_.proxy_host)) {
    Show(/*animate=*/true);
  }
  const std::optional<model::ProxyConfig> config = draft_.Proxy();
  if (!config) {
    test_.reset();
    tested_.reset();
    test_state_ = TestState::kFailed;
    test_text_ = base::UTF8ToUTF16(
        model::MessageOf(model::ProfileDraft::Problem::kPortInvalid));
    Show(/*animate=*/true);
    EndTestInterval();
    return;
  }
  tested_ = config;
  test_state_ = TestState::kTesting;
  test_text_ = u"Testing…";
  Show(/*animate=*/true);
  test_ = std::make_unique<ProxyTest>(
      *config,
      base::BindOnce(
          [](base::WeakPtr<ProfileSheet> sheet, model::ProxyConfig tested,
             const ProxyTest::Outcome& outcome) {
            if (sheet) {
              sheet->Tested(tested, outcome.ip, outcome.error);
            }
          },
          weak_factory_.GetWeakPtr(), *config));
}

void ProfileSheet::Tested(const model::ProxyConfig& tested,
                          std::optional<std::string> ip,
                          std::string error) {
  // The test goes once its answer is in; this runs in its last call, which
  // touches nothing of it after.
  test_.reset();
  if (draft_.Proxy() != tested) {
    testing_interval_.reset();
    return;
  }
  test_state_ = ip ? TestState::kConnected : TestState::kFailed;
  test_text_ = ip ? base::StrCat({u"Connected · ", base::UTF8ToUTF16(*ip)})
                  : base::UTF8ToUTF16(error);
  Show(/*animate=*/true);
  EndTestInterval();
}

void ProfileSheet::EndTestInterval() {
  const std::optional<uint64_t> interval =
      std::exchange(testing_interval_, std::nullopt);
  views::Widget* widget = GetWidget();
  if (interval && widget && widget->GetCompositor()) {
    EndInteractionAtNextFrame(Interaction::kTestProxy, *interval,
                              widget->GetCompositor());
  }
}

void ProfileSheet::ZonesListed(std::vector<model::TimeZoneRegion> regions) {
  std::vector<std::string> ids = {std::string()};
  std::vector<MenuButton::Option> options = {{.title = u"Automatic"}};
  std::vector<MenuButton::Entry> entries = {MenuButton::Entry::OptionAt(0),
                                            MenuButton::Entry::Separator()};
  for (const model::TimeZoneRegion& region : regions) {
    std::vector<size_t> indices;
    for (const model::TimeZoneChoice& choice : region.choices) {
      indices.push_back(options.size());
      ids.push_back(choice.identifier);
      options.push_back(
          {.title = base::UTF8ToUTF16(choice.title),
           .shown = base::UTF8ToUTF16(model::TimeZoneCity(choice.identifier))});
    }
    entries.push_back(MenuButton::Entry::Submenu(base::UTF8ToUTF16(region.name),
                                                 std::move(indices)));
  }
  entries.push_back(MenuButton::Entry::Separator());
  entries.push_back(MenuButton::Entry::OptionAt(options.size()));
  ids.push_back(model::kUTCTimeZone);
  options.push_back({.title = u"UTC"});
  zone_ids_ = std::move(ids);
  zone_->SetOptions(std::move(options), std::move(entries));
  const auto zone = std::ranges::find(zone_ids_, draft_.time_zone);
  zone_->SetSelected(zone == zone_ids_.end() ? std::nullopt
                                             : std::make_optional<size_t>(
                                                   zone - zone_ids_.begin()));
}

void ProfileSheet::ChooseZone(size_t index) {
  if (index < zone_ids_.size()) {
    draft_.time_zone = zone_ids_[index];
    Edited();
  }
}

void ProfileSheet::RevealProblem() {
  using Problem = model::ProfileDraft::Problem;
  if (!problem_) {
    return;
  }
  // Laid out for what it shows now.
  if (GetWidget()) {
    GetWidget()->LayoutRootViewIfNecessary();
  }
  views::View* target = problem_ == Problem::kNameMissing
                            ? name_block_.get()
                            : static_cast<views::View*>(address_.get());
  gfx::Rect place = views::View::ConvertRectToTarget(target, contents_,
                                                     target->GetLocalBounds());
  if (problem_ != Problem::kNameMissing) {
    place.Outset(gfx::Outsets::VH(kRevealMargin, 0));
  }
  const gfx::Rect visible = scroll_->GetVisibleRect();
  float offset = scroll_->CurrentOffset().y();
  if (place.y() < visible.y()) {
    offset = place.y();
  } else if (place.bottom() > visible.bottom()) {
    offset = place.bottom() - visible.height();
  } else {
    return;
  }
  scroll_motion_.MoveTo(scroll_->CurrentOffset().y(), motion::Reveal(),
                        nullptr);
  scroll_motion_.MoveTo(offset, motion::Reveal(),
                        GetWidget() ? GetWidget()->GetCompositor() : nullptr);
}

std::u16string ProfileSheet::HistoryNote() const {
  if (draft_.saves_history) {
    return u"Keeps the pages it visits and its downloads.";
  }
  // Saving clears only what there is to clear: on a profile that kept its
  // history until this edit.
  return saved_history_
             ? u"Keeps no pages or downloads. Saving clears what it kept."
             : u"Keeps no pages or downloads.";
}

std::u16string ProfileSheet::SpoofNote() const {
  if (!draft_.spoofing) {
    return base::StrCat({u"Sites see this ", Host(), u"."});
  }
  return base::StrCat(
      {u"Sites see a generic ", ClaimedDevice(draft_.platform), u", not this ",
       draft_.platform == model::HostPlatform() ? u"one" : Host(), u"."});
}

std::u16string ProfileSheet::ZoneNote() const {
  if (!draft_.time_zone.empty()) {
    return base::StrCat({u"Overrides this ", Host(), u"’s time zone."});
  }
  return base::StrCat(
      {u"Follows this ", Host(), u": ",
       base::UTF8ToUTF16(model::TimeZoneCity(model::CurrentTimeZone())), u"."});
}

std::vector<std::pair<std::string, const views::View*>>
ProfileSheet::NamedViews() const {
  std::vector<std::pair<std::string, const views::View*>> named;
  const auto add = [&](std::string name, const views::View* view) {
    if (view && view->IsDrawn()) {
      named.emplace_back(std::move(name), view);
    }
  };
  add("pane.profileSheet", this);
  add("pane.profile.close", close_);
  add("pane.profile.body", scroll_);
  add("pane.profile.name", name_);
  for (const Swatch* swatch : swatches_) {
    add("pane.profile.color." + ColorKey(swatch->color()), swatch);
  }
  add("pane.profile.history", history_);
  add("pane.profile.spoof", spoof_);
  for (const PlatformTile* tile : tiles_) {
    add("pane.profile.platform." +
            base::ToLowerASCII(model::TitleOf(tile->platform())),
        tile);
  }
  add("pane.profile.warning",
      warning_->GetVisible() ? warning_.get() : nullptr);
  add("pane.profile.timeZone", zone_);
  add("pane.profile.proxy", proxy_);
  add("pane.profile.proxyType", address_->type());
  add("pane.profile.proxyHost", address_->host());
  add("pane.profile.proxyPort", address_->port());
  add("pane.profile.proxyUsername", username_);
  add("pane.profile.proxyPassword", password_);
  add("pane.profile.testProxy", test_button_);
  add("pane.profile.proxyStatus", test_status_);
  add("pane.profile.cancel", cancel_);
  add("pane.profile.save", save_);
  return named;
}

MenuButton* ProfileSheet::FindMenu(std::string_view name) {
  if (name == "timeZone") {
    return zone_;
  }
  if (name == "proxyType") {
    return address_->type();
  }
  return nullptr;
}

views::Textfield* ProfileSheet::FindField(std::string_view name) {
  if (name == "name") {
    return name_;
  }
  if (name == "proxyHost") {
    return address_->host();
  }
  if (name == "proxyPort") {
    return address_->port();
  }
  if (name == "proxyUsername") {
    return username_;
  }
  if (name == "proxyPassword") {
    return password_;
  }
  return nullptr;
}

void ProfileSheet::Layout(PassKey) {
  title_->SetBounds(
      kInset, kHeaderPadding + (kHeaderRow - kTitle.line_height) / 2,
      std::max(width() - 2 * kInset - kCloseSize, 0), kTitle.line_height);
  close_->SetBounds(width() - kInset - kCloseSize, kHeaderPadding, kCloseSize,
                    kCloseSize);
  header_rule_->SetBounds(0, kHeaderHeight, width(), 1);

  const int footer_top = height() - kFooterHeight;
  footer_rule_->SetBounds(0, footer_top - 1, width(), 1);
  const gfx::Size save = save_->GetPreferredSize();
  const gfx::Size cancel = cancel_->GetPreferredSize();
  save_->SetBounds(width() - kInset - save.width(), footer_top + kFooterPadding,
                   save.width(), save.height());
  cancel_->SetBounds(save_->x() - kButtonSpacing - cancel.width(),
                     footer_top + kFooterPadding, cancel.width(),
                     cancel.height());

  scroll_->SetBounds(0, kHeaderHeight + 1, width(),
                     std::max(footer_top - 1 - kHeaderHeight - 1, 0));
}

BEGIN_METADATA(ProfileSheet)
END_METADATA

}  // namespace pane
