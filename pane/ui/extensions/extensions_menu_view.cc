// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/extensions/extensions_menu_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/icon_button.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/extensions/extension_icon.h"
#include "pane/ui/extensions/extensions_ui.h"
#include "pane/ui/view_ids.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"

namespace pane {

namespace {

// 4 inside the menu's 1-point ring, as CSS's p-1 within its border.
constexpr int kInset = 5;
constexpr int kRow = 32;
// A row's icon and title, 8 in and 8 apart; its trailing buttons 8 apart
// and 8 from its end.
constexpr int kRowLeading = 8;
constexpr int kRowIcon = 16;
constexpr int kRowGap = 8;
constexpr int kTrailing = 24;
// A group's heading: 24 tall, 6 in.
constexpr int kHeading = 24;
constexpr int kHeadingInset = 6;
// No installed extensions: 8 in.
constexpr int kEmptyInset = 8;
constexpr int kRule = 1;
// A row's glyph, 16 points, its lines 1.75 of the grid, as the Swift's
// RowIcon; an extension's letter at 10 on a tile rounded 7.
constexpr float kGlyphStroke = 1.75f;
constexpr int kLetter = 10;
constexpr float kLetterRadius = 7;

// A row: the accent fading in under the pointer, rounded 15, no press of its
// own (a plain button's), lit at once while its more menu shows.
ControlLook RowLook() {
  ControlLook look(ControlShape::Squircle(radius::kLg));
  look.hover_fill = kPaneColorAccent;
  look.press = SK_ColorTRANSPARENT;
  look.disabled_opacity = 1;
  return look;
}

// A trailing button: 24 points, a squircle of 11 that the foreground's 8%
// fills under the pointer; its icon keeps its own color (the pin's blue).
ControlLook TrailingLook(ui::ColorVariant tint) {
  ControlLook look(ControlShape::Squircle(radius::kMd));
  look.hover_fill = kPaneColorMenuButtonHover;
  look.tint = tint;
  look.lit_tint = tint;
  look.disabled_opacity = 0.4f;
  return look;
}

IconButton::Glyph PinGlyph(bool pinned) {
  return {pinned ? Icon::kPinOff : Icon::kPin, kRowIcon, kGlyphStroke};
}

}  // namespace

// A row of the menus (the Swift's MenuRow): a button from its leading edge to
// its trailing buttons, its icon and its title, cut short before them.
class ExtensionsMenuRow : public views::Button {
  METADATA_HEADER(ExtensionsMenuRow, views::Button)

 public:
  ExtensionsMenuRow(base::RepeatingClosure pressed,
                    int id,
                    std::optional<Icon> glyph,
                    const std::u16string& title)
      : views::Button(base::BindRepeating(&ExtensionsMenuRow::Pressed,
                                          base::Unretained(this))),
        pressed_(std::move(pressed)),
        glyph_(glyph) {
    SetID(id);
    const ControlLook look = RowLook();
    highlight_ = std::make_unique<ControlHighlight>(
        this, look.shape, look.disabled_opacity,
        base::BindRepeating(&ExtensionsMenuRow::SchedulePaint,
                            base::Unretained(this)));
    SetBackground(std::make_unique<ControlBackground>(look, highlight_.get()));
    title_ = AddChildView(
        MakeLabel(title, text::kMenuRow, kPaneColorAccentForeground));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetElideBehavior(gfx::ELIDE_TAIL);
    title_->SetCanProcessEventsWithinSubtree(false);
    // The row reads as its title, its icon unread.
    GetViewAccessibility().SetName(title);
  }

  void SetTitle(const std::u16string& title) {
    title_->SetText(title);
    GetViewAccessibility().SetName(title);
  }

  // An extension's: its icon or letter, dimmed with its action, and its
  // trailing pin and more, when it has an action; without one the row only
  // shows.
  void SetExtension(const InstalledExtension& extension,
                    ExtensionsUi* ui,
                    bool highlighted) {
    extension_ = extension;
    SetTitle(base::UTF8ToUTF16(extension.name));
    SetEnabled(extension.has_action);
    if (extension.has_action && ui && !pin_) {
      pin_ = AddChildView(std::make_unique<IconButton>(
          base::BindRepeating(
              [](ExtensionsUi* ui, ExtensionsMenuRow* row) {
                ui->PressPin(row->extension_.id);
              },
              base::Unretained(ui), base::Unretained(this)),
          kTrailing, PinGlyph(false), u"Pin to toolbar",
          TrailingLook(kPaneColorMutedForeground)));
      pin_->SetID(kViewIdExtensionsMenuPin);
    }
    if (pin_) {
      pin_->SetVisible(extension.has_action);
      pin_->SetGlyph(PinGlyph(extension.pinned));
      pin_->SetLook(TrailingLook(extension.pinned ? kPaneColorPinned
                                                  : kPaneColorMutedForeground));
      pin_->SetName(extension.pinned ? u"Unpin from toolbar"
                                     : u"Pin to toolbar");
      pin_->GetViewAccessibility().SetIsSelected(extension.pinned);
    }
    if (ui && !more_) {
      more_ = AddChildView(std::make_unique<IconButton>(
          base::BindRepeating(
              [](ExtensionsUi* ui, ExtensionsMenuRow* row) {
                ui->PressMore(row->extension_.id);
              },
              base::Unretained(ui), base::Unretained(this)),
          kTrailing, IconButton::Glyph{Icon::kEllipsis, kRowIcon, kGlyphStroke},
          u"More", TrailingLook(kPaneColorMutedForeground)));
      more_->SetID(kViewIdExtensionsMenuMore);
    }
    SetHighlighted(highlighted);
    InvalidateLayout();
    SchedulePaint();
  }

  void SetHighlighted(bool highlighted) {
    if (highlighted == highlighted_) {
      return;
    }
    highlighted_ = highlighted;
    if (!highlighted && GetState() != STATE_DISABLED) {
      SetState(IsMouseHovered() ? STATE_HOVERED : STATE_NORMAL);
    }
    highlight_->SetHeld(highlighted);
  }

  void SetGlyph(Icon glyph) {
    glyph_ = glyph;
    SchedulePaint();
  }

  const std::string& extension_id() const { return extension_.id; }
  views::View* pin() const { return pin_; }
  views::View* more() const { return more_; }

  // Where the label part ends: its trailing buttons stand past it.
  int LabelEnd() const {
    int end = width() - kRowGap;
    for (const views::View* button : {pin_.get(), more_.get()}) {
      if (button && button->GetVisible()) {
        end -= kTrailing + kRowGap;
      }
    }
    return end;
  }

  // views::Button:
  void Layout(PassKey) override {
    const int title_x = kRowLeading + kRowIcon + kRowGap;
    const int line = text::kMenuRow.line_height;
    title_->SetBounds(title_x, (height() - line) / 2,
                      std::max(LabelEnd() - title_x, 0), line);
    int x = width() - kRowGap;
    for (views::View* button : {more_.get(), pin_.get()}) {
      if (button && button->GetVisible()) {
        x -= kTrailing;
        button->SetBounds(x, (height() - kTrailing) / 2, kTrailing, kTrailing);
        x -= kRowGap;
      }
    }
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const gfx::RectF icon(kRowLeading, (height() - kRowIcon) / 2.0f, kRowIcon,
                          kRowIcon);
    if (glyph_) {
      PaintIcon(*canvas, *glyph_, icon, kGlyphStroke,
                colors->GetColor(kPaneColorMutedForeground));
      return;
    }
    // An action that doesn't show on the page dims its icon to 40%.
    const bool dimmed = extension_.has_action && !extension_.action_enabled;
    if (dimmed) {
      canvas->SaveLayerAlpha(AlphaOf(0.4f));
    }
    PaintExtensionIcon(*canvas, *colors, extension_.icon, extension_.name, icon,
                       kLetter, kLetterRadius);
    if (dimmed) {
      canvas->Restore();
    }
  }

 private:
  // A press past the label part, between or beside the trailing buttons, is
  // nobody's, as the Swift's row had only its label as a button.
  void Pressed(const ui::Event& event) {
    if (event.IsLocatedEvent() &&
        event.AsLocatedEvent()->location().x() >= LabelEnd()) {
      return;
    }
    pressed_.Run();
  }

  base::RepeatingClosure pressed_;
  std::optional<Icon> glyph_;
  InstalledExtension extension_;
  std::unique_ptr<ControlHighlight> highlight_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<IconButton> pin_ = nullptr;
  raw_ptr<IconButton> more_ = nullptr;
  bool highlighted_ = false;
};

BEGIN_METADATA(ExtensionsMenuRow)
END_METADATA

namespace {

// A group's heading, 12 points muted, a header to assistive technologies.
std::unique_ptr<views::Label> Heading(const std::u16string& text) {
  std::unique_ptr<views::Label> heading =
      MakeLabel(text, text::kSidebar, kPaneColorMutedForeground);
  heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  heading->SetElideBehavior(gfx::ELIDE_TAIL);
  heading->GetViewAccessibility().SetRole(ax::mojom::Role::kHeading);
  return heading;
}

// The rule between two groups, across the whole menu.
void PaintRule(gfx::Canvas& canvas,
               const ui::ColorProvider& colors,
               int width,
               int y) {
  cc::PaintFlags flags;
  flags.setColor(colors.GetColor(kPaneColorBorder));
  canvas.DrawRect(gfx::RectF(0, y, width, kRule), flags);
}

}  // namespace

ExtensionsMenuView::Shows::Shows() = default;
ExtensionsMenuView::Shows::Shows(const Shows&) = default;
ExtensionsMenuView::Shows& ExtensionsMenuView::Shows::operator=(const Shows&) =
    default;
ExtensionsMenuView::Shows::~Shows() = default;

// static
DialogCard::Look ExtensionsMenuView::CardLook() {
  // The popovers' surface (the Swift's popoverSurface): the popover's fill
  // rounded 22, the foreground's 10% just inside its edge, and the
  // popovers' shadow, 0 6px 20px.
  DialogCard::Look look;
  look.radius = radius::kXl;
  look.fill = kPaneColorPopover;
  look.edge = kPaneColorPopoverRing;
  look.edge_width = 1;
  look.edge_outside = false;
  look.shadow = kPaneColorPopoverShadow;
  look.shadow_offset = gfx::Vector2dF(0, 6);
  look.shadow_sigma = 10;
  return look;
}

ExtensionsMenuView::ExtensionsMenuView(ExtensionsUi& ui) : ui_(ui) {
  SetID(kViewIdExtensionsMenu);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(u"Extensions");
  manage_ = AddChildView(std::make_unique<ExtensionsMenuRow>(
      base::BindRepeating(&ExtensionsUi::PressManage, base::Unretained(&ui)),
      kViewIdExtensionsMenuManage, Icon::kPuzzle, u"Manage all extensions"));
  web_store_ = AddChildView(std::make_unique<ExtensionsMenuRow>(
      base::BindRepeating(&ExtensionsUi::PressWebStore, base::Unretained(&ui)),
      kViewIdExtensionsMenuWebStore, Icon::kDownload,
      u"Visit Chrome Web Store"));
  heading_ = AddChildView(Heading(u"Installed extensions"));
  empty_ = AddChildView(MakeLabel(u"No installed extensions", text::kMenuRow,
                                  kPaneColorMutedForeground));
  empty_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
}

ExtensionsMenuView::~ExtensionsMenuView() = default;

void ExtensionsMenuView::SetShows(const Shows& shows) {
  const size_t count = shows.extensions.size();
  const bool changed = count != rows_.size();
  while (rows_.size() > count) {
    RemoveChildViewT(rows_.back().ExtractAsDangling());
    rows_.pop_back();
  }
  while (rows_.size() < count) {
    rows_.push_back(AddChildView(std::make_unique<ExtensionsMenuRow>(
        base::BindRepeating(
            [](ExtensionsUi* ui, ExtensionsMenuView* menu, size_t index) {
              ui->PressRow(menu->rows_[index]->extension_id());
            },
            base::Unretained(&*ui_), base::Unretained(this), rows_.size()),
        kViewIdExtensionsMenuExtension, std::nullopt, std::u16string())));
  }
  for (size_t index = 0; index < count; ++index) {
    const InstalledExtension& extension = shows.extensions[index];
    rows_[index]->SetExtension(extension, &*ui_, shows.more == extension.id);
  }
  empty_->SetVisible(rows_.empty());
  if (changed) {
    PreferredSizeChanged();
  }
}

std::optional<gfx::Rect> ExtensionsMenuView::RowBounds(
    const std::string& extension_id) const {
  for (const raw_ptr<ExtensionsMenuRow>& row : rows_) {
    if (row->extension_id() == extension_id) {
      return row->bounds();
    }
  }
  return std::nullopt;
}

std::vector<std::pair<std::string, const views::View*>>
ExtensionsMenuView::NamedViews() const {
  std::vector<std::pair<std::string, const views::View*>> named = {
      {std::string(ViewIdName(kViewIdExtensionsMenu)), this},
      {std::string(ViewIdName(kViewIdExtensionsMenuManage)), manage_},
      {std::string(ViewIdName(kViewIdExtensionsMenuWebStore)), web_store_},
  };
  for (const raw_ptr<ExtensionsMenuRow>& row : rows_) {
    const std::string suffix = "." + row->extension_id();
    named.emplace_back(
        std::string(ViewIdName(kViewIdExtensionsMenuExtension)) + suffix,
        row.get());
    if (row->pin() && row->pin()->GetVisible()) {
      named.emplace_back(
          std::string(ViewIdName(kViewIdExtensionsMenuPin)) + suffix,
          row->pin());
    }
    if (row->more()) {
      named.emplace_back(
          std::string(ViewIdName(kViewIdExtensionsMenuMore)) + suffix,
          row->more());
    }
  }
  return named;
}

gfx::Size ExtensionsMenuView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int rows = std::max<int>(rows_.size(), 1);
  return gfx::Size(kWidth, 2 * kInset + 2 * kRow + kRule + kInset + kHeading +
                               rows * kRow + kInset);
}

void ExtensionsMenuView::Layout(PassKey) {
  const int row_width = width() - 2 * kInset;
  manage_->SetBounds(kInset, kInset, row_width, kRow);
  web_store_->SetBounds(kInset, kInset + kRow, row_width, kRow);
  int y = RuleY() + kRule + kInset;
  const int heading_line = text::kSidebar.line_height;
  heading_->SetBounds(kInset + kHeadingInset, y + (kHeading - heading_line) / 2,
                      row_width - 2 * kHeadingInset, heading_line);
  y += kHeading;
  const int line = text::kMenuRow.line_height;
  empty_->SetBounds(kInset + kEmptyInset, y + (kRow - line) / 2,
                    row_width - 2 * kEmptyInset, line);
  for (const raw_ptr<ExtensionsMenuRow>& row : rows_) {
    row->SetBounds(kInset, y, row_width, kRow);
    y += kRow;
  }
}

int ExtensionsMenuView::RuleY() const {
  return 2 * kInset + 2 * kRow;
}

void ExtensionsMenuView::OnPaint(gfx::Canvas* canvas) {
  if (const ui::ColorProvider* colors = GetColorProvider()) {
    PaintRule(*canvas, *colors, width(), RuleY());
  }
}

ExtensionMoreMenuView::ExtensionMoreMenuView(ExtensionsUi& ui) : ui_(ui) {
  SetID(kViewIdExtensionMoreMenu);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  heading_ = AddChildView(Heading(std::u16string()));
  const auto press = [](ExtensionsUi* ui, ExtensionMoreMenuView* menu,
                        void (ExtensionsUi::*method)(const std::string&)) {
    (ui->*method)(menu->extension_.id);
  };
  const auto row = [&](void (ExtensionsUi::*method)(const std::string&), int id,
                       Icon glyph, const std::u16string& title) {
    return AddChildView(std::make_unique<ExtensionsMenuRow>(
        base::BindRepeating(press, base::Unretained(&ui),
                            base::Unretained(this), method),
        id, glyph, title));
  };
  store_page_ =
      row(&ExtensionsUi::PressStorePage, kViewIdExtensionMoreMenuStorePage,
          Icon::kSquareArrowOutUpRight, std::u16string());
  remove_ = row(&ExtensionsUi::PressRemove, kViewIdExtensionMoreMenuRemove,
                Icon::kTrash2, u"Remove from Pane");
  pin_ = row(&ExtensionsUi::PressMorePin, kViewIdExtensionMoreMenuPin,
             Icon::kPin, u"Pin");
  manage_ =
      row(&ExtensionsUi::PressManageExtension, kViewIdExtensionMoreMenuManage,
          Icon::kSettings, u"Manage Extension");
  options_ = row(&ExtensionsUi::PressOptions, kViewIdExtensionMoreMenuOptions,
                 Icon::kSettings, u"Options");
}

ExtensionMoreMenuView::~ExtensionMoreMenuView() = default;

void ExtensionMoreMenuView::SetExtension(const InstalledExtension& extension) {
  const bool groups_changed =
      extension.has_action != extension_.has_action ||
      extension.options_url.is_valid() != extension_.options_url.is_valid();
  extension_ = extension;
  const std::u16string name = base::UTF8ToUTF16(extension.name);
  heading_->SetText(name);
  GetViewAccessibility().SetName(name);
  store_page_->SetTitle(name);
  pin_->SetVisible(extension.has_action);
  pin_->SetGlyph(extension.pinned ? Icon::kPinOff : Icon::kPin);
  pin_->SetTitle(extension.pinned ? u"Unpin" : u"Pin");
  options_->SetVisible(extension.options_url.is_valid());
  if (groups_changed) {
    PreferredSizeChanged();
  }
  InvalidateLayout();
}

std::vector<std::pair<std::string, const views::View*>>
ExtensionMoreMenuView::NamedViews() const {
  std::vector<std::pair<std::string, const views::View*>> named = {
      {std::string(ViewIdName(kViewIdExtensionMoreMenu)), this},
  };
  for (const ExtensionsMenuRow* row :
       {store_page_.get(), remove_.get(), pin_.get(), manage_.get(),
        options_.get()}) {
    if (row->GetVisible()) {
      named.emplace_back(std::string(ViewIdName(row->GetID())), row);
    }
  }
  return named;
}

gfx::Size ExtensionMoreMenuView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int second = extension_.has_action ? 2 : 1;
  const int third = extension_.options_url.is_valid() ? 2 : 1;
  return gfx::Size(kWidth, (2 * kInset + kHeading + kRow) + kRule +
                               (2 * kInset + second * kRow) + kRule +
                               (2 * kInset + third * kRow));
}

void ExtensionMoreMenuView::Layout(PassKey) {
  const int row_width = width() - 2 * kInset;
  int y = kInset;
  const int heading_line = text::kSidebar.line_height;
  heading_->SetBounds(kInset + kHeadingInset, y + (kHeading - heading_line) / 2,
                      row_width - 2 * kHeadingInset, heading_line);
  y += kHeading;
  store_page_->SetBounds(kInset, y, row_width, kRow);
  y += kRow + kInset + kRule + kInset;
  for (ExtensionsMenuRow* row : {remove_.get(), pin_.get()}) {
    if (row->GetVisible()) {
      row->SetBounds(kInset, y, row_width, kRow);
      y += kRow;
    }
  }
  y += kInset + kRule + kInset;
  for (ExtensionsMenuRow* row : {manage_.get(), options_.get()}) {
    if (row->GetVisible()) {
      row->SetBounds(kInset, y, row_width, kRow);
      y += kRow;
    }
  }
}

std::vector<int> ExtensionMoreMenuView::RuleYs() const {
  const int first = 2 * kInset + kHeading + kRow;
  const int second =
      first + kRule + 2 * kInset + (extension_.has_action ? 2 : 1) * kRow;
  return {first, second};
}

void ExtensionMoreMenuView::OnPaint(gfx::Canvas* canvas) {
  if (const ui::ColorProvider* colors = GetColorProvider()) {
    for (const int y : RuleYs()) {
      PaintRule(*canvas, *colors, width(), y);
    }
  }
}

BEGIN_METADATA(ExtensionsMenuView)
END_METADATA

BEGIN_METADATA(ExtensionMoreMenuView)
END_METADATA

}  // namespace pane
