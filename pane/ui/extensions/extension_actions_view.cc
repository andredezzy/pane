// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/extensions/extension_actions_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/icon_button.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/extensions/extension_icon.h"
#include "pane/ui/extensions/extensions_ui.h"
#include "pane/ui/view_ids.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// Aside's sizes (ADR 0060): 32-point circles 4 apart, flush inside the
// capsule's 1-point border; the puzzle 4 beside it.
constexpr int kButton = 32;
constexpr int kSpacing = 4;
constexpr int kBorder = 1;
// The icon at 16 points, as Chrome and Aside draw action icons; a letter at
// 9 on a tile rounded 4 without one (ADR 0054).
constexpr int kIcon = 16;
constexpr int kLetter = 9;
constexpr float kLetterRadius = 4;
// An action that doesn't show on the page dims to 40%.
constexpr float kHiddenActionOpacity = 0.4f;
// The puzzle: 1.4 of 24 at 20 points, the thin stroke the user chose for it.
constexpr IconButton::Glyph kPuzzle{Icon::kPuzzle, 20, 1.4f};

// The puzzle's look: lit with the menu open, its icon then the text's own
// color.
ControlLook PuzzleLook(bool open) {
  ControlLook look = IconButtonLook();
  look.lit_tint = open ? kPaneColorForeground : kPaneColorAccentForeground;
  return look;
}

}  // namespace

ExtensionActionsView::Shows::Shows() = default;
ExtensionActionsView::Shows::Shows(const Shows&) = default;
ExtensionActionsView::Shows& ExtensionActionsView::Shows::operator=(
    const Shows&) = default;
ExtensionActionsView::Shows::~Shows() = default;

// A pinned action (the Swift's ActionButton): a 32-point circle filled with
// the accent under the pointer, while its popup shows, and while its
// right-click menu does; its icon at 16 points, or its letter, and its badge.
class ExtensionActionsView::ActionButton : public views::Button,
                                           public views::ContextMenuController {
  METADATA_HEADER(ActionButton, views::Button)

 public:
  explicit ActionButton(ExtensionsUi& ui)
      : views::Button(base::BindRepeating(&ActionButton::Pressed,
                                          base::Unretained(this))),
        ui_(ui) {
    SetID(kViewIdExtensionAction);
    const ControlLook look = IconButtonLook();
    highlight_ = std::make_unique<ControlHighlight>(
        this, look.shape, look.disabled_opacity,
        base::BindRepeating(&ActionButton::SchedulePaint,
                            base::Unretained(this)));
    SetBackground(std::make_unique<ControlBackground>(look, highlight_.get()));
    set_context_menu_controller(this);
    // The whole button dims as one when its action doesn't show.
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }

  const std::string& id() const { return extension_.id; }

  void Set(const InstalledExtension& extension, bool popup, bool menu) {
    extension_ = extension;
    const std::u16string name = base::UTF8ToUTF16(extension.action_title.empty()
                                                      ? extension.name
                                                      : extension.action_title);
    SetTooltipText(name);
    GetViewAccessibility().SetName(name);
    GetViewAccessibility().SetIsSelected(popup);
    layer()->SetOpacity(extension.action_enabled ? 1 : kHiddenActionOpacity);
    SetHeld(popup || menu);
    SchedulePaint();
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kButton, kButton);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const gfx::RectF bounds(GetLocalBounds());
    PaintExtensionIcon(*canvas, *colors, extension_.icon, extension_.name,
                       gfx::RectF((width() - kIcon) / 2.0f,
                                  (height() - kIcon) / 2.0f, kIcon, kIcon),
                       kLetter, kLetterRadius);
    PaintBadge(*canvas, *colors, base::UTF8ToUTF16(extension_.badge_text),
               extension_.badge_background, extension_.badge_text_color,
               bounds);
  }

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override {
    ui_->ShowActionMenu(extension_.id, point, source_type);
  }

 private:
  void Pressed() { ui_->PressAction(extension_.id); }

  void SetHeld(bool held) {
    if (held == held_) {
      return;
    }
    held_ = held;
    // What it opened had the pointer's events: it hears where it is now.
    if (!held && GetState() != STATE_DISABLED) {
      SetState(IsMouseHovered() ? STATE_HOVERED : STATE_NORMAL);
    }
    highlight_->SetHeld(held);
  }

  const raw_ref<ExtensionsUi> ui_;
  InstalledExtension extension_;
  std::unique_ptr<ControlHighlight> highlight_;
  bool held_ = false;
};

BEGIN_METADATA(ExtensionActionsView, ActionButton)
END_METADATA

// The pinned actions' capsule: its ring, drawn over them, fades in while the
// pointer is over it (Motion.hover) and shows at once while one's popup does.
class ExtensionActionsView::Capsule : public views::View {
  METADATA_HEADER(Capsule, views::View)

 public:
  Capsule()
      : hover_(0,
               base::BindRepeating(
                   [](Capsule* capsule, double) { capsule->Repaint(); },
                   base::Unretained(this))) {
    SetID(kViewIdExtensionActions);
    SetNotifyEnterExitOnChild(true);
    // A group only to the eye: its buttons read on their own.
    GetViewAccessibility().SetIsIgnored(true);
  }

  void SetPopupShows(bool shows) {
    if (shows != popup_shows_) {
      popup_shows_ = shows;
      Repaint();
    }
  }

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override {
    hover_.MoveTo(1, motion::Hover(), Compositor());
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    hover_.MoveTo(0, motion::Hover(), Compositor());
  }

  // The ring just inside the capsule's edge: the buttons sit a point in
  // from it, so it shows over nothing of theirs, as the Swift's overlay.
  void OnPaint(gfx::Canvas* canvas) override {
    const float opacity = popup_shows_ ? 1.0f : hover_.value();
    const ui::ColorProvider* colors = GetColorProvider();
    if (opacity <= 0 || !colors) {
      return;
    }
    const SkColor border = colors->GetColor(kPaneColorBorder);
    cc::PaintFlags ring;
    ring.setAntiAlias(true);
    ring.setStyle(cc::PaintFlags::kStroke_Style);
    ring.setStrokeWidth(kBorder);
    ring.setColor(SkColorSetA(
        border, static_cast<U8CPU>(SkColorGetA(border) * opacity + 0.5f)));
    canvas->DrawPath(ControlShape::Capsule().StrokePathIn(
                         gfx::RectF(GetLocalBounds()), kBorder),
                     ring);
  }

 private:
  ui::Compositor* Compositor() {
    return GetWidget() ? GetWidget()->GetCompositor() : nullptr;
  }

  void Repaint() { SchedulePaint(); }

  ValueMotion hover_;
  bool popup_shows_ = false;
};

BEGIN_METADATA(ExtensionActionsView, Capsule)
END_METADATA

ExtensionActionsView::ExtensionActionsView(ExtensionsUi& ui) : ui_(ui) {
  capsule_ = AddChildView(std::make_unique<Capsule>());
  capsule_->SetVisible(false);
  puzzle_ = AddChildView(std::make_unique<IconButton>(
      base::BindRepeating(&ExtensionsUi::PressPuzzle, base::Unretained(&ui)),
      kButton, kPuzzle, u"Extensions", PuzzleLook(/*open=*/false)));
  puzzle_->SetID(kViewIdExtensions);
}

ExtensionActionsView::~ExtensionActionsView() = default;

void ExtensionActionsView::SetShows(const Shows& shows) {
  // The buttons follow the pinned actions; each keeps its hover while its
  // extension stays.
  bool same = shows.pinned.size() == buttons_.size();
  for (size_t index = 0; same && index < buttons_.size(); ++index) {
    same = buttons_[index]->id() == shows.pinned[index].id;
  }
  if (!same) {
    for (raw_ptr<ActionButton>& button : buttons_) {
      capsule_->RemoveChildViewT(button.ExtractAsDangling());
    }
    buttons_.clear();
    for (size_t index = 0; index < shows.pinned.size(); ++index) {
      buttons_.push_back(
          capsule_->AddChildView(std::make_unique<ActionButton>(*ui_)));
    }
    capsule_->SetVisible(!buttons_.empty());
    PreferredSizeChanged();
  }
  for (size_t index = 0; index < buttons_.size(); ++index) {
    const InstalledExtension& extension = shows.pinned[index];
    buttons_[index]->Set(extension, shows.popup == extension.id,
                         shows.menu_action == extension.id);
  }
  capsule_->SetPopupShows(shows.popup && ActionButtonFor(*shows.popup));
  if (shows.menu_open != menu_open_) {
    menu_open_ = shows.menu_open;
    puzzle_->SetLook(PuzzleLook(menu_open_));
    puzzle_->SetHeld(menu_open_);
    puzzle_->GetViewAccessibility().SetIsSelected(menu_open_);
  }
  if (!same) {
    InvalidateLayout();
  }
}

views::View* ExtensionActionsView::puzzle() {
  return puzzle_;
}

views::View* ExtensionActionsView::ActionButtonFor(
    const std::string& extension_id) {
  for (raw_ptr<ActionButton>& button : buttons_) {
    if (button->id() == extension_id) {
      return button;
    }
  }
  return nullptr;
}

std::vector<std::pair<std::string, const views::View*>>
ExtensionActionsView::NamedViews() const {
  std::vector<std::pair<std::string, const views::View*>> named = {
      {std::string(ViewIdName(kViewIdExtensions)), puzzle_},
      {std::string(ViewIdName(kViewIdExtensionActions)), capsule_},
  };
  for (const raw_ptr<ActionButton>& button : buttons_) {
    named.emplace_back(
        std::string(ViewIdName(kViewIdExtensionAction)) + "." + button->id(),
        button.get());
  }
  return named;
}

gfx::Size ExtensionActionsView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  int width = kButton;
  if (!buttons_.empty()) {
    const int count = static_cast<int>(buttons_.size());
    width += 2 * kBorder + count * kButton + (count - 1) * kSpacing + kSpacing;
  }
  return gfx::Size(width, kHeight);
}

void ExtensionActionsView::Layout(PassKey) {
  int x = 0;
  if (!buttons_.empty()) {
    const int count = static_cast<int>(buttons_.size());
    const int capsule_width =
        2 * kBorder + count * kButton + (count - 1) * kSpacing;
    capsule_->SetBounds(0, (height() - kHeight) / 2, capsule_width, kHeight);
    for (int index = 0; index < count; ++index) {
      buttons_[index]->SetBounds(kBorder + index * (kButton + kSpacing),
                                 kBorder, kButton, kButton);
    }
    x = capsule_width + kSpacing;
  }
  puzzle_->SetBounds(x, (height() - kButton) / 2, kButton, kButton);
}

BEGIN_METADATA(ExtensionActionsView)
END_METADATA

}  // namespace pane
