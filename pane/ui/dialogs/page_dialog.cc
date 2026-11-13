// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/page_dialog.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/field.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/scroll.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/design/text_button.h"
#include "pane/ui/view_ids.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/render_text.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/focus/focus_manager.h"

namespace pane {

namespace {

// The title in bold and the message at 13, each line 16 tall, the message's
// lines 2 apart; the title, the message, the fields, and the buttons 10
// apart, the buttons 6 more.
constexpr TextStyle kTitle{13, weight::kBold, 16};
constexpr TextStyle kMessage{13, weight::kRegular, 16};
constexpr int kLineSpacing = 2;
constexpr int kSpacing = 10;
constexpr int kButtonsTop = 6;
// A field: 22 tall, rounded 7, its text 7 in from its edge, as the alert's
// NSTextField sat 7 in its frame, and 8 between two.
constexpr int kFieldHeight = 22;
constexpr int kFieldTextInset = 7;
constexpr int kFieldSpacing = 8;
// A button: a 28-point capsule, 8 from the next.
constexpr int kButtonSpacing = 8;
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

// An alert's text field: white (black on dark) with a gray edge, 22 points
// tall, rounded 7; the edge turns blue once an edit begins in it, and back
// as it lets the keyboard go, as the alert's NSTextField's did (its
// controlTextDidBeginEditing comes with the first change, not the focus).
// Tab moves round the dialog's fields, not out of it.
class PageDialog::Field : public views::Textfield,
                          public views::TextfieldController {
  METADATA_HEADER(Field, views::Textfield)

 public:
  Field(const PageDialogContent::Field& field,
        base::RepeatingCallback<void(bool reverse)> tab)
      : tab_(std::move(tab)) {
    SetID(kViewIdDialogField);
    // Its fill and edge are this one's, painted below and over the text, so
    // Chromium's own background, border, ring, and ripple go.
    SetBackgroundEnabled(false);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, kFieldTextInset)));
    views::FocusRing::Remove(this);
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
    SetFontList(SystemFont(kMessage.size, kMessage.weight));
    SetTextColorId(kPaneColorAlertText);
    SetPlaceholderTextColorId(kPaneColorAlertPlaceholder);
    // Grayscale rather than subpixel text, as StyleLabel's (ADR 0060).
    GetRenderText()->set_subpixel_rendering_suppressed(true);
    if (field.secure) {
      SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
    }
    SetText(field.text);
    SetPlaceholderText(field.placeholder);
    // Its placeholder, as VoiceOver read the alert's field (ADR 0071).
    GetViewAccessibility().SetName(field.placeholder.empty()
                                       ? std::u16string(u"Text")
                                       : field.placeholder);
    set_controller(this);
  }

  // views::Textfield:
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kPaneColorAlertField));
    canvas->DrawPath(SquirclePath(gfx::RectF(GetLocalBounds()), radius::kSm),
                     flags);
  }

  void OnPaintBorder(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(colors->GetColor(editing_ ? kPaneColorAlertDefaultButton
                                             : kPaneColorAlertFieldEdge));
    canvas->DrawPath(
        SquircleStrokePath(gfx::RectF(GetLocalBounds()), radius::kSm, 1),
        flags);
  }

  void OnBlur() override {
    views::Textfield::OnBlur();
    if (std::exchange(editing_, false)) {
      SchedulePaint();
    }
  }

  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override {
    return event.key_code() == ui::VKEY_TAB ||
           views::Textfield::SkipDefaultKeyEventProcessing(event);
  }

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& event) override {
    if (event.type() != ui::EventType::kKeyPressed ||
        event.key_code() != ui::VKEY_TAB ||
        (event.flags() &
         (ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN))) {
      return false;
    }
    tab_.Run(event.IsShiftDown());
    return true;
  }

  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override {
    if (!std::exchange(editing_, true)) {
      SchedulePaint();
    }
  }

 private:
  const base::RepeatingCallback<void(bool reverse)> tab_;
  bool editing_ = false;
  // Selected text as an NSTextField draws it, dimmed while the window isn't
  // key.
  SelectionFollowsWindow selection_{this};
};

BEGIN_METADATA(PageDialog, Field)
END_METADATA

// static
DialogCard::Look PageDialog::AlertCardLook() {
  DialogCard::Look look;
  look.radius = radius::k2xl;
  look.fill = kPaneColorAlertPanel;
  look.edge = kPaneColorAlertEdge;
  look.edge_width = 0.5f;
  look.edge_outside = false;
  look.padding = gfx::Insets::TLBR(22, 22, 16, 16);
  look.shadow = kPaneColorAlertShadow;
  look.shadow_offset = gfx::Vector2dF(0, 16);
  look.shadow_sigma = 24;
  return look;
}

PageDialog::PageDialog(const PageDialogContent& content, Answer answer)
    : content_(content), answer_(std::move(answer)) {
  SetID(kViewIdDialog);
  // It holds the keys while no field has them, with no ring of its own.
  SetFocusBehavior(FocusBehavior::ALWAYS);
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetName(content_.title);
  GetViewAccessibility().SetIsModal(true);

  title_ = AddChildView(MakeLabel(content_.title, kTitle, kPaneColorAlertText));
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
          MakeLabel(line, kMessage, kPaneColorAlertText));
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
    fields_.push_back(AddChildView(std::make_unique<Field>(field, tab)));
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
    auto button = std::make_unique<TextButton>(
        base::BindRepeating(&PageDialog::Press, base::Unretained(this),
                            std::optional<size_t>(index)),
        content_.buttons[index],
        index == 0 ? ButtonVariant::kAlertDefault : ButtonVariant::kAlert,
        button_size::kAlert);
    button->SetID(kViewIdDialogButton);
    if (index == 0) {
      button->GetViewAccessibility().SetIsDefault(true);
    }
    buttons_[index] = AddChildView(std::move(button));
  }
}

PageDialog::~PageDialog() = default;

views::Textfield* PageDialog::field(size_t index) {
  return fields_[index];
}

std::vector<std::u16string> PageDialog::FieldTexts() const {
  std::vector<std::u16string> texts;
  for (const Field* field : fields_) {
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
  const int width = PageDialog::kWidth - AlertCardLook().padding.width();
  const std::optional<int> scrolled = scrolled_message_height_;
  scrolled_message_height_.reset();
  const int natural = HeightForWidth(width);
  if (message_ && natural > height) {
    const int others = natural - MessageHeight(width);
    scrolled_message_height_ =
        std::clamp(height - others, kMessage.line_height, kMostScrolledMessage);
    if (!message_scroll_) {
      // The message moves into a scroll view of its own, made only for one
      // this long.
      const size_t at = GetIndexOf(message_).value();
      std::unique_ptr<views::View> message = RemoveChildViewT(message_.get());
      std::unique_ptr<views::ScrollView> scroll = LayeredScrollView();
      scroll->SetBackgroundColor(std::nullopt);
      // An alert's message scrolls with no line at its edges.
      scroll->SetDrawOverflowIndicator(false);
      scroll->SetHorizontalScrollBarMode(
          views::ScrollView::ScrollBarMode::kDisabled);
      message_ = scroll->SetContents(std::move(message));
      message_scroll_ = AddChildViewAt(std::move(scroll), at);
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
  return height + kLineSpacing * (static_cast<int>(message_lines_.size()) - 1);
}

int PageDialog::FieldsHeight() const {
  const int count = static_cast<int>(fields_.size());
  return count == 0 ? 0 : count * kFieldHeight + (count - 1) * kFieldSpacing;
}

int PageDialog::ButtonsHeight() const {
  const int count = static_cast<int>(buttons_.size());
  const int height = button_size::kAlert.height;
  return count > 2 ? count * height + (count - 1) * kButtonSpacing : height;
}

int PageDialog::HeightForWidth(int width) const {
  int height = std::max(title_->GetHeightForWidth(width), kTitle.line_height);
  if (message_) {
    height +=
        kSpacing + scrolled_message_height_.value_or(MessageHeight(width));
  }
  if (!fields_.empty()) {
    height += kSpacing + FieldsHeight();
  }
  return height + kSpacing + kButtonsTop + ButtonsHeight();
}

gfx::Size PageDialog::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int width = available_size.width().is_bounded()
                        ? available_size.width().value()
                        : kWidth - AlertCardLook().padding.width();
  return gfx::Size(width, HeightForWidth(width));
}

void PageDialog::Layout(PassKey) {
  const int width = this->width();
  int y = 0;
  const int title =
      std::max(title_->GetHeightForWidth(width), kTitle.line_height);
  title_->SetBounds(0, y, width, title);
  y += title;
  if (message_) {
    // Each line on its own, 2 apart, as the Swift's VStack set them.
    int line_y = 0;
    for (views::Label* line : message_lines_) {
      const int height = line->GetHeightForWidth(width);
      line->SetBounds(0, line_y, width, height);
      line_y += height + kLineSpacing;
    }
    const int natural = MessageHeight(width);
    message_->SetSize(gfx::Size(width, natural));
    y += kSpacing;
    const int shown = scrolled_message_height_.value_or(natural);
    (message_scroll_ ? static_cast<views::View*>(message_scroll_.get())
                     : message_.get())
        ->SetBounds(0, y, width, shown);
    y += shown;
  }
  if (!fields_.empty()) {
    y += kSpacing;
    for (Field* field : fields_) {
      field->SetBounds(0, y, width, kFieldHeight);
      y += kFieldHeight + kFieldSpacing;
    }
    y -= kFieldSpacing;
  }
  y += kSpacing + kButtonsTop;
  const int height = button_size::kAlert.height;
  if (buttons_.size() > 2) {
    for (views::Button* button : buttons_) {
      button->SetBounds(0, y, width, height);
      y += height + kButtonSpacing;
    }
    return;
  }
  // Side by side, sharing the row, the default on the right.
  const int count = static_cast<int>(buttons_.size());
  const int each = (width - (count - 1) * kButtonSpacing) / std::max(count, 1);
  for (int index = 0; index < count; ++index) {
    const int place = count - 1 - index;
    const int x =
        place == count - 1 ? width - each : place * (each + kButtonSpacing);
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
  if (std::ranges::any_of(fields_, [focused](const raw_ptr<Field>& field) {
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
    static_cast<Field*>(stops[next])->SelectAll(/*reversed=*/false);
  }
}

BEGIN_METADATA(PageDialog)
END_METADATA

}  // namespace pane
