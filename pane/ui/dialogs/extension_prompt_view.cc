// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/extension_prompt_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/checkbox.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/design/text_button.h"
#include "pane/ui/view_ids.h"
#include "skia/ext/image_operations.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_solid_color.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"

namespace pane {

namespace {

// Each card's content inside its 1-point edge: Dia's 24, Aside's 20.
constexpr int kInstallPadding = 24 + 1;
constexpr int kRemovePadding = 20 + 1;

// The icons: 32 points, the arrow 16 between them, 8 apart; rounded 15 where
// an extension has no icon of its own.
constexpr int kIcon = 32;
constexpr int kArrow = 16;
constexpr int kIconSpacing = 8;
constexpr float kArrowStroke = 2;
constexpr float kIconPlaceholderRadius = radius::kLg;

// Adding: the title 14 under the icons, the panel 12 under it, the buttons
// 24 under that, 8 apart.
constexpr TextStyle kInstallTitle{20, weight::kSemibold, 26};
constexpr int kInstallTitleTop = 14;
constexpr int kPanelTop = 12;
constexpr int kInstallButtonsTop = 24;
constexpr int kButtonSpacing = 8;

// The panel: 12 by 14 in, its lines at 14 on 20, 2 apart; a bullet 8 in,
// its text 8 after it.
constexpr TextStyle kPermission{14, weight::kRegular, 20};
constexpr int kPanelPaddingY = 12;
constexpr int kPanelPaddingX = 14;
constexpr int kPanelSpacing = 2;
constexpr int kBulletIndent = 8;
constexpr int kBulletSpacing = 8;

// Removing: the icon and the title 20 apart, on one line; then 20, and
// Report abuse 4 further; then 20, and the buttons 12 further.
constexpr TextStyle kRemoveTitle{15, weight::kMedium, 20};
constexpr TextStyle kReportAbuse{13, weight::kRegular, 16};
constexpr int kRemoveTitleSpacing = 20;
constexpr int kRemoveSpacing = 20;
constexpr int kReportAbuseTop = 4;
constexpr int kRemoveButtonsTop = 12;

// `image` drawn at `size` points, resampled as SwiftUI's .high interpolation
// did; empty stays empty.
gfx::ImageSkia Resized(const gfx::ImageSkia& image, int size) {
  return image.isNull() ? image
                        : gfx::ImageSkiaOperations::CreateResizedImage(
                              image, skia::ImageOperations::RESIZE_BEST,
                              gfx::Size(size, size));
}

}  // namespace

ExtensionPrompt::ExtensionPrompt() = default;
ExtensionPrompt::ExtensionPrompt(ExtensionPrompt&&) = default;
ExtensionPrompt& ExtensionPrompt::operator=(ExtensionPrompt&&) = default;
ExtensionPrompt::~ExtensionPrompt() = default;

std::string_view ExtensionPromptKindName(ExtensionPrompt::Kind kind) {
  switch (kind) {
    case ExtensionPrompt::Kind::kInstall:
      return "extensionInstall";
    case ExtensionPrompt::Kind::kRemove:
      return "extensionRemove";
  }
}

// The icons' row, which assistive technologies skip, as the Swift's did:
// adding, Pane's, the arrow, and the extension's; removing, the extension's.
class ExtensionPromptView::Icons : public views::View {
  METADATA_HEADER(Icons, views::View)

 public:
  Icons(const gfx::ImageSkia& app_icon, const gfx::Image& extension_icon)
      : app_icon_(Resized(app_icon, kIcon)),
        extension_icon_(Resized(extension_icon.IsEmpty()
                                    ? gfx::ImageSkia()
                                    : extension_icon.AsImageSkia(),
                                kIcon)),
        shows_app_(!app_icon.isNull()) {
    GetViewAccessibility().SetIsIgnored(true);
  }

  // Adding shows Pane's icon and the arrow before the extension's.
  void SetShowsApp(bool shows_app) { shows_app_ = shows_app; }

  // Pane's icon, as the Dock shows it now.
  void SetAppIcon(const gfx::ImageSkia& app_icon) {
    app_icon_ = Resized(app_icon, kIcon);
    SchedulePaint();
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(shows_app_
                         ? kIcon + kIconSpacing + kArrow + kIconSpacing + kIcon
                         : kIcon,
                     kIcon);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    int x = 0;
    if (shows_app_) {
      canvas->DrawImageInt(app_icon_, 0, 0);
      x += kIcon + kIconSpacing;
      PaintIcon(*canvas, Icon::kArrowLeft,
                gfx::RectF(x, (kIcon - kArrow) / 2.0f, kArrow, kArrow),
                kArrowStroke, colors->GetColor(kPaneColorMutedForeground));
      x += kArrow + kIconSpacing;
    }
    if (!extension_icon_.isNull()) {
      canvas->DrawImageInt(extension_icon_, x, 0);
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kPaneColorSecondary));
    canvas->DrawPath(
        SquirclePath(gfx::RectF(x, 0, kIcon, kIcon), kIconPlaceholderRadius),
        flags);
  }

 private:
  gfx::ImageSkia app_icon_;
  const gfx::ImageSkia extension_icon_;
  bool shows_app_;
};

BEGIN_METADATA(ExtensionPromptView, Icons)
END_METADATA

// What adding lets the extension do, on its panel: the heading, then each
// permission after its bullet, wrapping under its own start.
class ExtensionPromptView::Permissions : public views::View {
  METADATA_HEADER(Permissions, views::View)

 public:
  Permissions(const std::u16string& heading,
              const std::vector<std::u16string>& permissions) {
    heading_ = AddChildView(
        MakeLabel(heading, kPermission, kPaneColorAccentForeground));
    heading_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    heading_->SetMultiLine(true);
    // Read in the prompt, which holds the keys: the heading and each
    // permission, not its bullet.
    ExposeToScreenReaders(*heading_);
    for (const std::u16string& permission : permissions) {
      Row row;
      row.bullet = AddChildView(
          MakeLabel(u"•", kPermission, kPaneColorAccentForeground));
      row.bullet->GetViewAccessibility().SetIsIgnored(true);
      row.text = AddChildView(
          MakeLabel(permission, kPermission, kPaneColorAccentForeground));
      row.text->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      row.text->SetMultiLine(true);
      ExposeToScreenReaders(*row.text);
      rows_.push_back(row);
    }
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int width =
        available_size.width().is_bounded()
            ? available_size.width().value()
            : ExtensionPromptView::kInstallWidth - 2 * kInstallPadding;
    return gfx::Size(width, Arrange(width, /*place=*/false));
  }

  void Layout(PassKey) override { Arrange(width(), /*place=*/true); }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kPaneColorPanelTint));
    canvas->DrawPath(SquirclePath(gfx::RectF(GetLocalBounds()), radius::kLg),
                     flags);
  }

 private:
  struct Row {
    raw_ptr<views::Label> bullet = nullptr;
    raw_ptr<views::Label> text = nullptr;
  };

  int Arrange(int width, bool place) const {
    const int inner = std::max(width - 2 * kPanelPaddingX, 0);
    int y = kPanelPaddingY;
    const int heading = heading_->GetHeightForWidth(inner);
    if (place) {
      heading_->SetBounds(kPanelPaddingX, y, inner, heading);
    }
    y += heading;
    for (const Row& row : rows_) {
      y += kPanelSpacing;
      const int bullet = row.bullet->GetPreferredSize().width();
      const int text_x =
          kPanelPaddingX + kBulletIndent + bullet + kBulletSpacing;
      const int text_width = std::max(kPanelPaddingX + inner - text_x, 0);
      const int text = row.text->GetHeightForWidth(text_width);
      if (place) {
        row.bullet->SetBounds(kPanelPaddingX + kBulletIndent, y, bullet,
                              kPermission.line_height);
        row.text->SetBounds(text_x, y, text_width, text);
      }
      y += text;
    }
    return y + kPanelPaddingY;
  }

  raw_ptr<views::Label> heading_ = nullptr;
  std::vector<Row> rows_;
};

BEGIN_METADATA(ExtensionPromptView, Permissions)
END_METADATA

ExtensionPromptView::ExtensionPromptView(
    const ExtensionPrompt& prompt,
    const gfx::ImageSkia& app_icon,
    base::RepeatingCallback<void(bool accepted)> pressed)
    : kind_(prompt.kind) {
  const bool install = kind_ == ExtensionPrompt::Kind::kInstall;
  SetID(install ? kViewIdExtensionInstall : kViewIdExtensionPopup);
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetName(prompt.title);
  GetViewAccessibility().SetIsModal(true);

  icons_ = AddChildView(std::make_unique<Icons>(
      install ? app_icon : gfx::ImageSkia(), prompt.icon));
  icons_->SetShowsApp(install);
  title_ = AddChildView(MakeLabel(prompt.title,
                                  install ? kInstallTitle : kRemoveTitle,
                                  kPaneColorForeground));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetMultiLine(true);
  // The dialog reads by its title.
  title_->GetViewAccessibility().SetIsIgnored(true);

  if (install && !prompt.permissions.empty()) {
    permissions_ = AddChildView(std::make_unique<Permissions>(
        prompt.permissions_heading, prompt.permissions));
  }
  if (!install) {
    report_abuse_ = AddChildView(std::make_unique<Checkbox>(
        u"Report abuse", kReportAbuse, kPaneColorAccentForeground));
    report_abuse_->SetID(kViewIdExtensionPopupReportAbuse);
  }

  const auto press = [](base::RepeatingCallback<void(bool)> pressed,
                        bool accepted) { pressed.Run(accepted); };
  std::unique_ptr<views::View> cancel;
  std::unique_ptr<views::View> accept;
  if (install) {
    cancel = std::make_unique<KeyedButton>(
        base::BindRepeating(press, pressed, false), prompt.cancel_title, u"Esc",
        KeyedButton::Role::kCancel);
    accept = std::make_unique<KeyedButton>(
        base::BindRepeating(press, pressed, true), prompt.accept_title,
        // Its text form, as the App icon picker's: bare, it falls back to the
        // emoji.
        u"↩\uFE0E", KeyedButton::Role::kPrimary);
  } else {
    cancel = std::make_unique<TextButton>(
        base::BindRepeating(press, pressed, false), prompt.cancel_title,
        ButtonVariant::kPopupCancel, button_size::kPopup);
    accept = std::make_unique<TextButton>(
        base::BindRepeating(press, pressed, true), prompt.accept_title,
        ButtonVariant::kPopupPrimary, button_size::kPopup);
  }
  cancel->SetID(kViewIdExtensionPopupCancel);
  accept->SetID(kViewIdExtensionPopupAccept);
  accept->GetViewAccessibility().SetIsDefault(true);
  cancel_ = AddChildView(std::move(cancel));
  accept_ = AddChildView(std::move(accept));
}

ExtensionPromptView::~ExtensionPromptView() = default;

DialogCard::Look ExtensionPromptView::CardLook(ExtensionPrompt::Kind kind) {
  DialogCard::Look look;
  look.radius = radius::k2xl;
  look.edge = kPaneColorPopoverRing;
  look.edge_width = 1;
  look.edge_outside = false;
  if (kind == ExtensionPrompt::Kind::kInstall) {
    // Dia's own surface (ADR 0058), not Quit Pane?'s ring outside the card.
    look.fill = kPaneColorCard;
    look.padding = gfx::Insets(kInstallPadding);
    look.shadow = kPaneColorInstallDialogShadow;
    look.shadow_offset = gfx::Vector2dF(0, 24);
    look.shadow_sigma = 32;
  } else {
    // The popovers' surface (the Swift's popoverSurface): 0 6px 20px.
    look.fill = kPaneColorPopover;
    look.padding = gfx::Insets(kRemovePadding);
    look.shadow = kPaneColorPopoverShadow;
    look.shadow_offset = gfx::Vector2dF(0, 6);
    look.shadow_sigma = 10;
  }
  return look;
}

int ExtensionPromptView::WidthOf(ExtensionPrompt::Kind kind) {
  return kind == ExtensionPrompt::Kind::kInstall ? kInstallWidth : kRemoveWidth;
}

bool ExtensionPromptView::report_abuse() const {
  return report_abuse_ && report_abuse_->checked();
}

void ExtensionPromptView::SetAppIcon(const gfx::ImageSkia& app_icon) {
  if (kind_ == ExtensionPrompt::Kind::kInstall && icons_) {
    icons_->SetAppIcon(app_icon);
  }
}

std::vector<std::pair<std::string, const views::View*>>
ExtensionPromptView::NamedViews() const {
  std::vector<std::pair<std::string, const views::View*>> named = {
      {std::string(ViewIdName(GetID())), this},
      {std::string(ViewIdName(kViewIdExtensionPopupAccept)), accept_.get()},
      {std::string(ViewIdName(kViewIdExtensionPopupCancel)), cancel_.get()},
  };
  if (report_abuse_) {
    named.emplace_back(
        std::string(ViewIdName(kViewIdExtensionPopupReportAbuse)),
        report_abuse_.get());
  }
  return named;
}

gfx::Size ExtensionPromptView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int width =
      available_size.width().is_bounded()
          ? available_size.width().value()
          : WidthOf(kind_) - 2 * (kind_ == ExtensionPrompt::Kind::kInstall
                                      ? kInstallPadding
                                      : kRemovePadding);
  return gfx::Size(width, Arrange(width, /*place=*/false));
}

void ExtensionPromptView::Layout(PassKey) {
  Arrange(width(), /*place=*/true);
}

int ExtensionPromptView::Arrange(int width, bool place) const {
  return kind_ == ExtensionPrompt::Kind::kInstall ? ArrangeInstall(width, place)
                                                  : ArrangeRemove(width, place);
}

int ExtensionPromptView::ArrangeInstall(int width, bool place) const {
  int y = 0;
  if (place) {
    icons_->SetBounds(0, y, icons_->GetPreferredSize().width(), kIcon);
  }
  y += kIcon + kInstallTitleTop;
  const int title = title_->GetHeightForWidth(width);
  if (place) {
    title_->SetBounds(0, y, width, title);
  }
  y += title;
  if (permissions_) {
    y += kPanelTop;
    const int panel = permissions_->GetHeightForWidth(width);
    if (place) {
      permissions_->SetBounds(0, y, width, panel);
    }
    y += panel;
  }
  y += kInstallButtonsTop;
  return ArrangeButtons(width, y, place);
}

int ExtensionPromptView::ArrangeRemove(int width, bool place) const {
  // The icon and the title, centered on one another, as an HStack does.
  const int title_width = std::max(width - kIcon - kRemoveTitleSpacing, 0);
  const int title = title_->GetHeightForWidth(title_width);
  const int row = std::max(kIcon, title);
  if (place) {
    icons_->SetBounds(0, (row - kIcon) / 2, kIcon, kIcon);
    title_->SetBounds(kIcon + kRemoveTitleSpacing, (row - title) / 2,
                      title_width, title);
  }
  int y = row + kRemoveSpacing + kReportAbuseTop;
  const gfx::Size box = report_abuse_->GetPreferredSize();
  if (place) {
    report_abuse_->SetBounds(0, y, std::min(box.width(), width), box.height());
  }
  y += box.height() + kRemoveSpacing + kRemoveButtonsTop;
  return ArrangeButtons(width, y, place);
}

int ExtensionPromptView::ArrangeButtons(int width, int y, bool place) const {
  const gfx::Size accept = accept_->GetPreferredSize();
  const gfx::Size cancel = cancel_->GetPreferredSize();
  if (place) {
    accept_->SetBounds(width - accept.width(), y, accept.width(),
                       accept.height());
    cancel_->SetBounds(accept_->x() - kButtonSpacing - cancel.width(), y,
                       cancel.width(), cancel.height());
  }
  return y + std::max(accept.height(), cancel.height());
}

BEGIN_METADATA(ExtensionPromptView)
END_METADATA

namespace {

// The popup: 4 under its anchor, 8 from the window's leading edge at least.
constexpr int kPopupGap = 4;
constexpr int kPopupMargin = 8;

}  // namespace

ExtensionPromptOverlay::ExtensionPromptOverlay(
    const ExtensionPrompt& prompt,
    const gfx::ImageSkia& app_icon,
    base::RepeatingCallback<void(bool accepted)> answer)
    : answer_(std::move(answer)) {
  // It holds the keys while it shows, and reads as nothing of its own: the
  // prompt is the dialog.
  SetFocusBehavior(FocusBehavior::ALWAYS);
  GetViewAccessibility().SetIsIgnored(true);
  if (prompt.kind == ExtensionPrompt::Kind::kInstall) {
    scrim_ = AddChildView(std::make_unique<views::View>());
    scrim_->SetPaintToLayer(ui::LAYER_SOLID_COLOR);
    scrim_->layer()->SetFillsBoundsOpaquely(false);
    scrim_->SetCanProcessEventsWithinSubtree(false);
  }
  auto view = std::make_unique<ExtensionPromptView>(prompt, app_icon, answer_);
  prompt_ = view.get();
  card_ = AddChildView(std::make_unique<DialogCard>(
      ExtensionPromptView::CardLook(prompt.kind), std::move(view)));
  // There at once, as the Swift's showed it.
  card_->Settle(true);
}

ExtensionPromptOverlay::~ExtensionPromptOverlay() = default;

void ExtensionPromptOverlay::SetAnchor(const std::optional<gfx::Rect>& anchor) {
  if (anchor_ != anchor) {
    anchor_ = anchor;
    InvalidateLayout();
  }
}

gfx::Rect ExtensionPromptOverlay::CardBounds() const {
  return card_->CardBounds();
}

void ExtensionPromptOverlay::Layout(PassKey) {
  if (scrim_) {
    scrim_->SetBoundsRect(GetLocalBounds());
  }
  const gfx::SizeF size =
      card_->SizeFor(ExtensionPromptView::WidthOf(prompt_->kind()));
  gfx::RectF place((width() - size.width()) / 2, (height() - size.height()) / 2,
                   size.width(), size.height());
  if (prompt_->kind() == ExtensionPrompt::Kind::kRemove && anchor_) {
    place.set_origin(gfx::PointF(
        std::max<float>(kPopupMargin, anchor_->right() - size.width()),
        anchor_->bottom() + kPopupGap));
  }
  if (card_->Place(place)) {
    card_->Settle(true);
  }
}

void ExtensionPromptOverlay::OnThemeChanged() {
  views::View::OnThemeChanged();
  if (scrim_) {
    // Black in both appearances, darker on dark, as Quit Pane?'s.
    scrim_->layer()->AsSolidColor()->SetColor(SkColor4f::FromColor(
        GetColorProvider()->GetColor(kPaneColorModalScrim)));
  }
}

// A press outside the card, or on its shadow: the card takes its own.
bool ExtensionPromptOverlay::OnMousePressed(const ui::MouseEvent& event) {
  answer_.Run(false);
  return true;
}

// Every key but a shortcut, which the window's commands take first, stops
// here; Esc and Return have come to the prompt already.
bool ExtensionPromptOverlay::OnKeyPressed(const ui::KeyEvent& event) {
  return true;
}

BEGIN_METADATA(ExtensionPromptOverlay)
END_METADATA

}  // namespace pane
