// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/menu_button.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/menu_source_utils.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button_controller.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// PaneMenuTrigger's small size: 32 points, 13 in from its start and 11 from
// its end (12 and 10 inside its 1-point border), its parts 8 apart; the
// icon 14 points, its lines 1.75 of the grid; the chevrons 13, at Lucide's 2.
constexpr int kHeight = 32;
constexpr int kLeading = 13;
constexpr int kTrailing = 11;
constexpr int kSpacing = 8;
constexpr int kIconSize = 14;
constexpr float kIconStroke = 1.75f;
constexpr int kChevronSize = 13;
constexpr float kChevronStroke = 2;

ControlLook TriggerLook() {
  ControlLook look(ControlShape::Squircle(radius::kMd));
  look.hover_fill = kPaneColorOutlineHover;
  look.border = kPaneColorBorder;
  return look;
}

}  // namespace

MenuButton::Opening::Opening() = default;
MenuButton::Opening::Opening(Opening&&) = default;
MenuButton::Opening& MenuButton::Opening::operator=(Opening&&) = default;
MenuButton::Opening::~Opening() = default;

MenuButton::MenuButton(std::u16string label,
                       std::vector<Option> options,
                       base::RepeatingCallback<void(size_t index)> chosen)
    : options_(std::move(options)),
      chosen_(std::move(chosen)),
      look_(TriggerLook()) {
  SetCallback(
      base::BindRepeating(&MenuButton::ShowMenu, base::Unretained(this)));
  // A menu opens as the press goes down, as the system's pop-up buttons do,
  // so a drag carries on into it.
  button_controller()->set_notify_action(
      views::ButtonController::NotifyAction::kOnPress);
  highlight_ = std::make_unique<ControlHighlight>(
      this, look_.shape, look_.disabled_opacity,
      base::BindRepeating(&views::View::SchedulePaint, base::Unretained(this)));
  SetBackground(std::make_unique<ControlBackground>(look_, highlight_.get()));
  title_ = AddChildView(
      MakeLabel(std::u16string(), text::kMenuValue, kPaneColorForeground));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetCanProcessEventsWithinSubtree(false);
  for (size_t index = 0; index < options_.size(); ++index) {
    menu_model_.AddCheckItem(static_cast<int>(index), options_[index].title);
  }
  GetViewAccessibility().SetRole(ax::mojom::Role::kPopUpButton);
  GetViewAccessibility().SetName(std::move(label));
  GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
}

MenuButton::~MenuButton() = default;

void MenuButton::SetSelected(std::optional<size_t> index) {
  if (index && *index >= options_.size()) {
    index.reset();
  }
  if (selected_ == index && shown_once_) {
    return;
  }
  shown_once_ = true;
  selected_ = index;
  const std::u16string& title =
      index ? options_[*index].title : base::EmptyString16();
  title_->SetText(title);
  GetViewAccessibility().SetValue(title);
  PreferredSizeChanged();
  SchedulePaint();
}

void MenuButton::SetOpening(Opening opening) {
  opening_ = std::move(opening);
}

gfx::RectF MenuButton::IconBounds() const {
  return gfx::RectF(kLeading, (height() - kIconSize) / 2.0f, kIconSize,
                    kIconSize);
}

gfx::RectF MenuButton::ChevronBounds() const {
  return gfx::RectF(width() - kTrailing - kChevronSize,
                    (height() - kChevronSize) / 2.0f, kChevronSize,
                    kChevronSize);
}

gfx::Size MenuButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const bool icon = selected_ && options_[*selected_].icon.has_value();
  return gfx::Size(kLeading + (icon ? kIconSize + kSpacing : 0) +
                       title_->GetPreferredSize().width() + kSpacing +
                       kChevronSize + kTrailing,
                   kHeight);
}

void MenuButton::Layout(PassKey) {
  const bool icon = selected_ && options_[*selected_].icon.has_value();
  const int start = kLeading + (icon ? kIconSize + kSpacing : 0);
  const int end = static_cast<int>(ChevronBounds().x()) - kSpacing;
  title_->SetBounds(start, 0, std::max(end - start, 0), height());
}

void MenuButton::PaintButtonContents(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  if (selected_ && options_[*selected_].icon) {
    PaintIcon(*canvas, *options_[*selected_].icon, IconBounds(), kIconStroke,
              colors->GetColor(kPaneColorForeground));
  }
  PaintIcon(*canvas, Icon::kChevronsUpDown, ChevronBounds(), kChevronStroke,
            colors->GetColor(kPaneColorMutedForeground));
}

void MenuButton::OnThemeChanged() {
  views::Button::OnThemeChanged();
  SchedulePaint();
}

bool MenuButton::IsCommandIdChecked(int command_id) const {
  return selected_ && static_cast<int>(*selected_) == command_id;
}

void MenuButton::ExecuteCommand(int command_id, int event_flags) {
  // Once the menu has gone, as what it changes may change this.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MenuButton> button, size_t index) {
            if (button) {
              button->chosen_.Run(index);
            }
          },
          weak_factory_.GetWeakPtr(), static_cast<size_t>(command_id)));
}

void MenuButton::OnMenuWillShow(ui::SimpleMenuModel* source) {
  if (opening_.shows) {
    opening_.shows.Run();
  }
}

void MenuButton::ShowMenu(const ui::Event& event) {
  if (!GetWidget() || (menu_runner_ && menu_runner_->IsRunning())) {
    return;
  }
  if (opening_.asked) {
    opening_.asked.Run();
  }
  // Lit while its menu shows, as the rows are; the menu takes the press's
  // release, so the press's own look goes now.
  SetState(STATE_HOVERED);
  highlight_->SetHeld(true);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      &menu_model_, views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(&MenuButton::MenuGone, weak_factory_.GetWeakPtr()));
  // Just under the trigger, its leading edge on the trigger's: the system's
  // menu shows a context menu's top left at the point it is given.
  gfx::Point anchor = GetLocalBounds().bottom_left();
  views::View::ConvertPointToScreen(this, &anchor);
  menu_runner_->RunMenuAt(GetWidget(), nullptr, gfx::Rect(anchor, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft,
                          ui::GetMenuSourceTypeForEvent(event));
}

void MenuButton::MenuGone() {
  // The menu had the pointer's events: the trigger hears where it is now.
  SetState(IsMouseHovered() ? STATE_HOVERED : STATE_NORMAL);
  highlight_->SetHeld(false);
}

BEGIN_METADATA(MenuButton)
END_METADATA

}  // namespace pane
