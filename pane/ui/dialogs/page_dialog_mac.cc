// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/page_dialog.h"

#include <memory>
#include <utility>

#include "base/no_destructor.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/field.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/design/text_button.h"
#include "pane/ui/view_ids.h"
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
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"

namespace pane {

namespace {

// The message at 13, as the alert's; its field's text in it.
constexpr TextStyle kMessage{13, weight::kRegular, 16};
// A field: its text 7 in from its edge, as the alert's NSTextField sat 7 in
// its frame.
constexpr int kFieldTextInset = 7;

// An alert's text field: white (black on dark) with a gray edge, 22 points
// tall, rounded 7; the edge turns blue once an edit begins in it, and back
// as it lets the keyboard go, as the alert's NSTextField's did (its
// controlTextDidBeginEditing comes with the first change, not the focus).
// Tab moves round the dialog's fields, not out of it.
class AlertField : public views::Textfield, public views::TextfieldController {
  METADATA_HEADER(AlertField, views::Textfield)

 public:
  AlertField(const PageDialogContent::Field& field,
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

BEGIN_METADATA(AlertField)
END_METADATA

}  // namespace

// The alert sheet macOS draws (Paper 04.15 to 04.23): 260 points wide, on
// the alert's card, the panel at 95% with its hairline edge, rounded 29, its
// content 22 in at the top and leading edge and 16 at the others, and its
// shadow, black at 25%, 16 down, a Gaussian of 24; the title in bold and the
// message at 13, each line 16 tall, the message's lines 2 apart; the title,
// the message, the fields, and the buttons 10 apart, the buttons 6 more; a
// field 22 tall, 8 from the next; 28-point capsules, 8 apart, two sharing
// the row. A long message ends hard at its edges.
const PageDialogStyle& ThisPageDialogStyle() {
  static const base::NoDestructor<PageDialogStyle> style([] {
    PageDialogStyle style;
    style.width = 260;
    style.card.radius = radius::k2xl;
    style.card.fill = kPaneColorAlertPanel;
    style.card.edge = kPaneColorAlertEdge;
    style.card.edge_width = 0.5f;
    style.card.edge_outside = false;
    style.card.padding = gfx::Insets::TLBR(22, 22, 16, 16);
    style.card.shadow = kPaneColorAlertShadow;
    style.card.shadow_offset = gfx::Vector2dF(0, 16);
    style.card.shadow_sigma = 24;
    style.title = {13, weight::kBold, 16};
    style.title_color = kPaneColorAlertText;
    style.message = kMessage;
    style.message_color = kPaneColorAlertText;
    style.title_spacing = 10;
    style.line_spacing = 2;
    style.spacing = 10;
    style.buttons_top = 6;
    style.button_spacing = 8;
    style.field_spacing = 8;
    style.field_height = 22;
    style.buttons_share_row = true;
    return style;
  }());
  return *style;
}

// A capsule, gray, or blue for the default (DialogButtonStyle); the alert
// shows no keys.
std::unique_ptr<views::Button> MakePageDialogButton(
    views::Button::PressedCallback pressed,
    const std::u16string& title,
    PageDialogButtonRole role) {
  return std::make_unique<TextButton>(std::move(pressed), title,
                                      role == PageDialogButtonRole::kDefault
                                          ? ButtonVariant::kAlertDefault
                                          : ButtonVariant::kAlert,
                                      button_size::kAlert);
}

std::unique_ptr<views::Textfield> MakePageDialogField(
    const PageDialogContent::Field& field,
    base::RepeatingCallback<void(bool reverse)> tab) {
  return std::make_unique<AlertField>(field, std::move(tab));
}

}  // namespace pane
