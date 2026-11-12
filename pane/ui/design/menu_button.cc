// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/menu_button.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "pane/ui/design/anchored_menu.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/menu_source_utils.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button_controller.h"
#include "ui/views/controls/label.h"
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
// Embedded in a field: 12 in and 10 from its end inside the field's border,
// its parts 6 apart, its chevrons 12 points.
constexpr int kEmbeddedLeading = 12;
constexpr int kEmbeddedTrailing = 10;
constexpr int kEmbeddedSpacing = 6;
constexpr int kEmbeddedChevronSize = 12;

ControlLook TriggerLook(MenuButton::Style style) {
  // A segment of a field takes the field's border and corners: its hover's
  // fill is a rectangle the field clips.
  if (style == MenuButton::Style::kEmbedded) {
    ControlLook look(ControlShape::Squircle(0));
    look.hover_fill = kPaneColorOutlineHover;
    return look;
  }
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

MenuButton::Entry::Entry() = default;
MenuButton::Entry::Entry(const Entry&) = default;
MenuButton::Entry& MenuButton::Entry::operator=(const Entry&) = default;
MenuButton::Entry::Entry(Entry&&) = default;
MenuButton::Entry& MenuButton::Entry::operator=(Entry&&) = default;
MenuButton::Entry::~Entry() = default;

// static
MenuButton::Entry MenuButton::Entry::OptionAt(size_t index) {
  Entry entry;
  entry.kind = Kind::kOption;
  entry.option = index;
  return entry;
}

// static
MenuButton::Entry MenuButton::Entry::Separator() {
  Entry entry;
  entry.kind = Kind::kSeparator;
  return entry;
}

// static
MenuButton::Entry MenuButton::Entry::Submenu(std::u16string title,
                                             std::vector<size_t> options) {
  Entry entry;
  entry.kind = Kind::kSubmenu;
  entry.title = std::move(title);
  entry.options = std::move(options);
  return entry;
}

MenuButton::MenuButton(std::u16string label,
                       std::vector<Option> options,
                       base::RepeatingCallback<void(size_t index)> chosen,
                       Style style)
    : options_(std::move(options)),
      chosen_(std::move(chosen)),
      style_(style),
      look_(TriggerLook(style)) {
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
  BuildMenu({});
  GetViewAccessibility().SetRole(ax::mojom::Role::kPopUpButton);
  GetViewAccessibility().SetName(std::move(label));
  GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
}

MenuButton::~MenuButton() = default;

void MenuButton::SetOptions(std::vector<Option> options,
                            std::vector<Entry> entries) {
  options_ = std::move(options);
  BuildMenu(entries);
  // The choice, by its place, shows as the option there now.
  const std::optional<size_t> selected = selected_;
  shown_once_ = false;
  SetSelected(selected);
}

void MenuButton::BuildMenu(const std::vector<Entry>& entries) {
  menu_model_.Clear();
  submenus_.clear();
  const auto add_option = [this](ui::SimpleMenuModel& menu, size_t index) {
    if (index < options_.size()) {
      menu.AddCheckItem(static_cast<int>(index), options_[index].title);
    }
  };
  if (entries.empty()) {
    for (size_t index = 0; index < options_.size(); ++index) {
      add_option(menu_model_, index);
    }
  }
  // A submenu's own command, past every option's.
  int submenu_command = static_cast<int>(options_.size());
  for (const Entry& entry : entries) {
    switch (entry.kind) {
      case Entry::Kind::kOption:
        add_option(menu_model_, entry.option);
        break;
      case Entry::Kind::kSeparator:
        menu_model_.AddSeparator(ui::NORMAL_SEPARATOR);
        break;
      case Entry::Kind::kSubmenu: {
        auto submenu = std::make_unique<ui::SimpleMenuModel>(this);
        for (const size_t index : entry.options) {
          add_option(*submenu, index);
        }
        menu_model_.AddSubMenu(submenu_command++, entry.title, submenu.get());
        submenus_.push_back(std::move(submenu));
        break;
      }
    }
  }
  if (style_ == Style::kEmbedded) {
    widest_title_ = 0;
    for (const Option& option : options_) {
      widest_title_ = std::max(
          widest_title_,
          gfx::GetStringWidth(
              option.shown.empty() ? option.title : option.shown,
              SystemFont(text::kMenuValue.size, text::kMenuValue.weight)));
    }
  }
}

const std::u16string& MenuButton::ShownTitle(size_t index) const {
  const Option& option = options_[index];
  return option.shown.empty() ? option.title : option.shown;
}

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
      index ? ShownTitle(*index) : base::EmptyString16();
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
  const bool embedded = style_ == Style::kEmbedded;
  const int size = embedded ? kEmbeddedChevronSize : kChevronSize;
  return gfx::RectF(width() - (embedded ? kEmbeddedTrailing : kTrailing) - size,
                    (height() - size) / 2.0f, size, size);
}

gfx::Size MenuButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  if (style_ == Style::kEmbedded) {
    // As tall as the field it is a segment of, which lays it out.
    return gfx::Size(kEmbeddedLeading + widest_title_ + kEmbeddedSpacing +
                         kEmbeddedChevronSize + kEmbeddedTrailing,
                     kHeight);
  }
  const bool icon = selected_ && options_[*selected_].icon.has_value();
  return gfx::Size(kLeading + (icon ? kIconSize + kSpacing : 0) +
                       title_->GetPreferredSize().width() + kSpacing +
                       kChevronSize + kTrailing,
                   kHeight);
}

void MenuButton::Layout(PassKey) {
  if (style_ == Style::kEmbedded) {
    title_->SetBounds(kEmbeddedLeading, 0, widest_title_, height());
    return;
  }
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
  PaintIcon(
      *canvas, Icon::kChevronsUpDown, ChevronBounds(), kChevronStroke,
      colors->GetColor(style_ == Style::kEmbedded ? kPaneColorMutedLifted
                                                  : kPaneColorMutedForeground));
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
  if (!GetWidget() || (menu_ && menu_->IsRunning())) {
    return;
  }
  if (opening_.asked) {
    opening_.asked.Run();
  }
  // Lit while its menu shows, as the rows are; the menu takes the press's
  // release, so the press's own look goes now.
  SetState(STATE_HOVERED);
  highlight_->SetHeld(true);
  menu_ = AnchoredMenu::Make(
      menu_model_,
      base::BindRepeating(&MenuButton::MenuGone, weak_factory_.GetWeakPtr()));
  // On the Mac it returns once the menu has closed, this perhaps deleted by
  // then: nothing follows it.
  menu_->Run(*GetWidget(), MenuAnchorUnder(GetBoundsInScreen()),
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
