// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/page_dialog.h"

#include <memory>
#include <utility>

#include "base/no_destructor.h"
#include "pane/ui/design/dialog.h"
#include "pane/ui/design/field.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/system_words.h"
#include "pane/ui/design/text.h"
#include "pane/ui/design/text_button.h"
#include "pane/ui/view_ids.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/textfield/textfield_controller.h"

namespace pane {

namespace {

// Pane's own field (Paper 04.17.1, 04.21.1): 36 points, rounded 11, on the
// window's background, its ring outside its border while it has the keys;
// Tab moves round the dialog's fields and buttons, not out of it.
class DialogField : public Field, public views::TextfieldController {
  METADATA_HEADER(DialogField, Field)

 public:
  DialogField(const PageDialogContent::Field& field,
              base::RepeatingCallback<void(bool reverse)> tab)
      : tab_(std::move(tab)) {
    SetID(kViewIdDialogField);
    if (field.secure) {
      SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
    }
    SetText(field.text);
    SetPlaceholderText(field.placeholder);
    // Its placeholder, as a screen reader reads it (ADR 0071).
    GetViewAccessibility().SetName(field.placeholder.empty()
                                       ? std::u16string(u"Text")
                                       : field.placeholder);
    set_controller(this);
  }

  // views::Textfield:
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override {
    return event.key_code() == ui::VKEY_TAB ||
           Field::SkipDefaultKeyEventProcessing(event);
  }

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& event) override {
    if (event.type() != ui::EventType::kKeyPressed ||
        event.key_code() != ui::VKEY_TAB ||
        (event.flags() & (ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN))) {
      return false;
    }
    tab_.Run(event.IsShiftDown());
    return true;
  }

 private:
  const base::RepeatingCallback<void(bool reverse)> tab_;
};

BEGIN_METADATA(DialogField)
END_METADATA

}  // namespace

// The card of Pane's own questions over the page's dim (Paper 04.15.1 to
// 04.23.1, 04.29.1, 11.18, the user's call): 480 points wide, as Exit Pane?
// is, its content 24 in; the title at 18 semibold on 24-point lines, 6 over
// the message at 14 on 20-point lines in the dialog's message color, its
// lines 2 apart; the heading, the fields, and the buttons 16 apart, the
// buttons 8 more; Pane's 36-point fields 8 apart; keyed buttons at their own
// widths at the row's end, 8 apart. No icon tile: it marks Pane's own
// questions, and a page's dialog carries the page's words alone, as
// Chrome's do. A long message fades 24 points at the edge it runs past,
// under Windows 11's overlay bar (11.18.1). The page under it dims, the card
// centred on it (11.18).
const PageDialogStyle& ThisPageDialogStyle() {
  static const base::NoDestructor<PageDialogStyle> style([] {
    PageDialogStyle style;
    style.width = 480;
    style.card = ModalDialog::CardLook();
    style.title = {18, weight::kSemibold, 24};
    style.title_color = kPaneColorForeground;
    style.message = {14, weight::kRegular, 20};
    style.message_color = kPaneColorDialogMessage;
    style.title_spacing = 6;
    style.line_spacing = 2;
    style.spacing = 16;
    style.buttons_top = 8;
    style.button_spacing = 8;
    style.field_spacing = 8;
    style.field_height = Field::kHeight;
    style.buttons_share_row = false;
    style.message_fade = 24;
    style.dims_page = true;
    style.centred_on_page = true;
    return style;
  }());
  return *style;
}

// Keyed, in their roles (Paper 11.18's table): the default filled, with
// Enter; Cancel, which Esc answers as, outlined with Esc; any other outlined
// with no key (a permission's Allow This Time and Don't Allow, which Esc
// leaves unanswered; an unresponsive page's Exit Page, Esc waiting).
std::unique_ptr<views::Button> MakePageDialogButton(
    views::Button::PressedCallback pressed,
    const std::u16string& title,
    PageDialogButtonRole role) {
  switch (role) {
    case PageDialogButtonRole::kDefault:
      return std::make_unique<KeyedButton>(std::move(pressed), title,
                                           std::u16string(Words().return_key),
                                           KeyedButton::Role::kPrimary);
    case PageDialogButtonRole::kEscape:
      return std::make_unique<KeyedButton>(std::move(pressed), title, u"Esc",
                                           KeyedButton::Role::kCancel);
    case PageDialogButtonRole::kOther:
      return std::make_unique<KeyedButton>(std::move(pressed), title,
                                           std::u16string(),
                                           KeyedButton::Role::kCancel);
  }
}

std::unique_ptr<views::Textfield> MakePageDialogField(
    const PageDialogContent::Field& field,
    base::RepeatingCallback<void(bool reverse)> tab) {
  return std::make_unique<DialogField>(field, std::move(tab));
}

}  // namespace pane
