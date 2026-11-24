// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/page_dialog.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/scroll.h"
#include "pane/ui/design/scroll_fade.h"
#include "pane/ui/design/text.h"
#include "pane/ui/view_ids.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"

namespace pane {

namespace {

// A message that scrolls shows at most this much of itself.
constexpr int kMostScrolledMessage = 320;

}  // namespace

PageDialogContent::PageDialogContent() = default;
PageDialogContent::PageDialogContent(const PageDialogContent&) = default;
PageDialogContent& PageDialogContent::operator=(const PageDialogContent&) =
    default;
PageDialogContent::PageDialogContent(PageDialogContent&&) = default;
PageDialogContent& PageDialogContent::operator=(PageDialogContent&&) = default;
PageDialogContent::~PageDialogContent() = default;

PageDialogStyle::PageDialogStyle() = default;
PageDialogStyle::PageDialogStyle(const PageDialogStyle&) = default;
PageDialogStyle& PageDialogStyle::operator=(const PageDialogStyle&) = default;
PageDialogStyle::~PageDialogStyle() = default;

PageDialog::PageDialog(const PageDialogContent& content, Answer answer)
    : content_(content),
      style_(ThisPageDialogStyle()),
      answer_(std::move(answer)) {
  SetID(kViewIdDialog);
  // It holds the keys while no field has them, with no ring of its own.
  SetFocusBehavior(FocusBehavior::ALWAYS);
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetName(content_.title);
  GetViewAccessibility().SetIsModal(true);

  title_ =
      AddChildView(MakeLabel(content_.title, style_.title, style_.title_color));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetMultiLine(true);
  // The title names the dialog; VoiceOver reads it once.
  title_->GetViewAccessibility().SetIsIgnored(true);

  if (std::ranges::any_of(content_.message, [](const std::u16string& line) {
        return !line.empty();
      })) {
    message_ = AddChildView(std::make_unique<views::View>());
    for (const std::u16string& line : content_.message) {
      views::Label* label = message_->AddChildView(
          MakeLabel(line, style_.message, style_.message_color));
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      label->SetMultiLine(true);
      // What the page says, read in the dialog, which holds the keys.
      ExposeToScreenReaders(*label);
      message_lines_.push_back(label);
    }
  }

  const auto tab =
      base::BindRepeating(&PageDialog::MoveFocus, base::Unretained(this));
  for (const PageDialogContent::Field& field : content_.fields) {
    fields_.push_back(AddChildView(MakePageDialogField(field, tab)));
  }

  // The buttons in the order they show, which Tab follows: two side by side,
  // the default on the right; three stacked, the default on top.
  buttons_.resize(content_.buttons.size());
  std::vector<size_t> order(content_.buttons.size());
  for (size_t index = 0; index < order.size(); ++index) {
    order[index] =
        content_.buttons.size() > 2 ? index : order.size() - 1 - index;
  }
  for (size_t index : order) {
    const PageDialogButtonRole role =
        index == 0                        ? PageDialogButtonRole::kDefault
        : index == content_.escape_button ? PageDialogButtonRole::kEscape
                                          : PageDialogButtonRole::kOther;
    std::unique_ptr<views::Button> button = MakePageDialogButton(
        base::BindRepeating(&PageDialog::Press, base::Unretained(this),
                            std::optional<size_t>(index)),
        content_.buttons[index], role);
    button->SetID(kViewIdDialogButton);
    if (index == 0) {
      button->GetViewAccessibility().SetIsDefault(true);
    }
    buttons_[index] = AddChildView(std::move(button));
  }
  focus_trap_ = std::make_unique<FocusTrap>(this);
}

PageDialog::~PageDialog() = default;

views::FocusTraversable* PageDialog::GetPaneFocusTraversable() {
  return retired_ ? nullptr : focus_trap_.get();
}

views::Textfield* PageDialog::field(size_t index) {
  return fields_[index];
}

std::vector<std::u16string> PageDialog::FieldTexts() const {
  std::vector<std::u16string> texts;
  for (const views::Textfield* field : fields_) {
    texts.emplace_back(field->GetText());
  }
  return texts;
}

void PageDialog::SetFieldTexts(const std::vector<std::u16string>& texts) {
  for (size_t index = 0; index < std::min(texts.size(), fields_.size());
       ++index) {
    ui::AXActionData action;
    action.action = ax::mojom::Action::kSetValue;
    action.value = base::UTF16ToUTF8(texts[index]);
    fields_[index]->HandleAccessibleAction(action);
  }
}

std::vector<std::pair<std::string, const views::View*>> PageDialog::NamedViews()
    const {
  std::vector<std::pair<std::string, const views::View*>> named;
  named.emplace_back(std::string(ViewIdName(kViewIdDialog)), this);
  for (size_t index = 0; index < fields_.size(); ++index) {
    named.emplace_back(std::string(ViewIdName(kViewIdDialogField)) + "." +
                           base::NumberToString(index),
                       fields_[index]);
  }
  for (size_t index = 0; index < buttons_.size(); ++index) {
    named.emplace_back(std::string(ViewIdName(kViewIdDialogButton)) + "." +
                           base::NumberToString(index),
                       buttons_[index]);
  }
  return named;
}

void PageDialog::TakeFocus() {
  if (fields_.empty()) {
    RequestFocus();
    return;
  }
  // As an alert's first field takes the keyboard, its text selected.
  fields_.front()->RequestFocus();
  fields_.front()->SelectAll(/*reversed=*/false);
}

void PageDialog::PressKey(bool escape) {
  Press(escape ? std::nullopt : std::optional<size_t>(0));
}

void PageDialog::Retire() {
  retired_ = true;
}

void PageDialog::SetMostHeight(int height) {
  if (height == most_height_) {
    return;
  }
  most_height_ = height;
  const int width = style_.width - style_.card.padding.width();
  const std::optional<int> scrolled = scrolled_message_height_;
  scrolled_message_height_.reset();
  const int natural = HeightForWidth(width);
  if (message_ && natural > height) {
    const int others = natural - MessageHeight(width);
    scrolled_message_height_ = std::clamp(
        height - others, style_.message.line_height, kMostScrolledMessage);
    if (!message_scroller_) {
      // The message moves into a scroll view of its own, made only for one
      // this long.
      const size_t at = GetIndexOf(message_).value();
      std::unique_ptr<views::View> message = RemoveChildViewT(message_.get());
      if (style_.message_fade > 0) {
        // Under the system's overlay bar, fading at the edge it runs past, an
        // edge nothing else marks (DESIGN.md › Scroll edges; Paper 11.18.1).
        auto fading = std::make_unique<FadingScrollView>(
            style_.card.fill, /*fades_top=*/true, /*fades_bottom=*/true,
            style_.message_fade);
        TakeSystemScrollBars(*fading->scroll_view());
        message_ = fading->scroll_view()->SetContents(std::move(message));
        message_scroller_ = AddChildViewAt(std::move(fading), at);
      } else {
        std::unique_ptr<views::ScrollView> scroll = LayeredScrollView();
        scroll->SetBackgroundColor(std::nullopt);
        // An alert's message scrolls with no line at its edges.
        scroll->SetDrawOverflowIndicator(false);
        scroll->SetHorizontalScrollBarMode(
            views::ScrollView::ScrollBarMode::kDisabled);
        message_ = scroll->SetContents(std::move(message));
        message_scroller_ = AddChildViewAt(std::move(scroll), at);
      }
    }
  }
  if (scrolled != scrolled_message_height_) {
    PreferredSizeChanged();
  }
}

int PageDialog::MessageHeight(int width) const {
  if (!message_) {
    return 0;
  }
  int height = 0;
  for (const views::Label* line : message_lines_) {
    height += line->GetHeightForWidth(width);
  }
  return height +
         style_.line_spacing * (static_cast<int>(message_lines_.size()) - 1);
}

int PageDialog::FieldsHeight() const {
  const int count = static_cast<int>(fields_.size());
  return count == 0
             ? 0
             : count * style_.field_height + (count - 1) * style_.field_spacing;
}

int PageDialog::ButtonsHeight() const {
  const int count = static_cast<int>(buttons_.size());
  const int height =
      buttons_.empty() ? 0 : buttons_.front()->GetPreferredSize().height();
  return count > 2 ? count * height + (count - 1) * style_.button_spacing
                   : height;
}

int PageDialog::HeightForWidth(int width) const {
  int height =
      std::max(title_->GetHeightForWidth(width), style_.title.line_height);
  if (message_) {
    height += style_.title_spacing +
              scrolled_message_height_.value_or(MessageHeight(width));
  }
  if (!fields_.empty()) {
    height += style_.spacing + FieldsHeight();
  }
  return height + style_.spacing + style_.buttons_top + ButtonsHeight();
}

gfx::Size PageDialog::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int width = available_size.width().is_bounded()
                        ? available_size.width().value()
                        : style_.width - style_.card.padding.width();
  return gfx::Size(width, HeightForWidth(width));
}

void PageDialog::Layout(PassKey) {
  const int width = this->width();
  int y = 0;
  const int title =
      std::max(title_->GetHeightForWidth(width), style_.title.line_height);
  title_->SetBounds(0, y, width, title);
  y += title;
  if (message_) {
    // Each line on its own, 2 apart (Paper 04.15).
    int line_y = 0;
    for (views::Label* line : message_lines_) {
      const int height = line->GetHeightForWidth(width);
      line->SetBounds(0, line_y, width, height);
      line_y += height + style_.line_spacing;
    }
    const int natural = MessageHeight(width);
    message_->SetSize(gfx::Size(width, natural));
    y += style_.title_spacing;
    const int shown = scrolled_message_height_.value_or(natural);
    (message_scroller_ ? message_scroller_.get() : message_.get())
        ->SetBounds(0, y, width, shown);
    y += shown;
  }
  if (!fields_.empty()) {
    y += style_.spacing;
    for (views::Textfield* field : fields_) {
      field->SetBounds(0, y, width, style_.field_height);
      y += style_.field_height + style_.field_spacing;
    }
    y -= style_.field_spacing;
  }
  y += style_.spacing + style_.buttons_top;
  if (buttons_.empty()) {
    return;
  }
  const int height = buttons_.front()->GetPreferredSize().height();
  if (buttons_.size() > 2) {
    for (views::Button* button : buttons_) {
      button->SetBounds(0, y, width, height);
      y += height + style_.button_spacing;
    }
    return;
  }
  const int count = static_cast<int>(buttons_.size());
  if (!style_.buttons_share_row) {
    // Each at its own width, at the row's end, the default on the right.
    int x = width;
    for (views::Button* button : buttons_) {
      const int each = button->GetPreferredSize().width();
      x -= each;
      button->SetBounds(x, y, each, height);
      x -= style_.button_spacing;
    }
    return;
  }
  // Side by side, sharing the row, the default on the right.
  const int each =
      (width - (count - 1) * style_.button_spacing) / std::max(count, 1);
  for (int index = 0; index < count; ++index) {
    const int place = count - 1 - index;
    const int x = place == count - 1 ? width - each
                                     : place * (each + style_.button_spacing);
    buttons_[index]->SetBounds(x, y, each, height);
  }
}

void PageDialog::Press(std::optional<size_t> button) {
  if (!retired_) {
    answer_.Run(button);
  }
}

bool PageDialog::SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) {
  return event.key_code() == ui::VKEY_TAB;
}

bool PageDialog::OnKeyPressed(const ui::KeyEvent& event) {
  // A shortcut goes on to the window's commands.
  if (event.flags() &
      (ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN)) {
    return false;
  }
  if (event.key_code() == ui::VKEY_TAB) {
    MoveFocus(event.IsShiftDown());
    return true;
  }
  // What is typed into a field is the field's: the Mac hands each key to the
  // views before the field's text input inserts it, and a key taken here
  // would never be typed (prompt(), a site's or a proxy's sign-in).
  const views::FocusManager* focus = GetFocusManager();
  const views::View* focused = focus ? focus->GetFocusedView() : nullptr;
  if (std::ranges::any_of(fields_,
                          [focused](const raw_ptr<views::Textfield>& field) {
                            return field.get() == focused;
                          })) {
    return false;
  }
  // No other key reaches what is under it.
  return true;
}

void PageDialog::MoveFocus(bool reverse) {
  std::vector<views::View*> stops;
  for (views::View* child : children()) {
    if ((child->GetID() == kViewIdDialogField ||
         child->GetID() == kViewIdDialogButton) &&
        child->IsFocusable()) {
      stops.push_back(child);
    }
  }
  if (stops.empty()) {
    return;
  }
  const views::FocusManager* focus = GetFocusManager();
  const views::View* focused = focus ? focus->GetFocusedView() : nullptr;
  const auto at = std::ranges::find(stops, focused);
  size_t next = 0;
  if (at != stops.end()) {
    const size_t index = at - stops.begin();
    next = reverse ? (index + stops.size() - 1) % stops.size()
                   : (index + 1) % stops.size();
  } else if (reverse) {
    next = stops.size() - 1;
  }
  stops[next]->RequestFocus();
  if (stops[next]->GetID() == kViewIdDialogField) {
    static_cast<views::Textfield*>(stops[next])->SelectAll(/*reversed=*/false);
  }
}

BEGIN_METADATA(PageDialog)
END_METADATA

}  // namespace pane
