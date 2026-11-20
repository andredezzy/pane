// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/pin_field.h"

#include <utility>

#include "ui/base/ime/text_input_type.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/focus_ring.h"

namespace pane {

PinField::PinField(base::RepeatingCallback<bool(const ui::KeyEvent&)> key,
                   base::RepeatingCallback<bool()> holds_keys)
    : key_(std::move(key)), holds_keys_(std::move(holds_keys)) {
  set_controller(this);
  // No menu: the menu key and Shift+F10 open the focused view's, and a
  // screen reader's Show menu asks it too, which put a text field's Paste
  // over the Lock (commit f475f04).
  set_context_menu_controller(nullptr);
  SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
  SetBackgroundEnabled(false);
  SetBorder(nullptr);
  SetCursorEnabled(false);
  views::FocusRing::Remove(this);
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetName(u"PIN");
}

PinField::~PinField() = default;

void PinField::SetDigits(int count) {
  SetText(std::u16string(static_cast<size_t>(count), u'0'));
}

bool PinField::SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) {
  return holds_keys_.Run();
}

void PinField::InsertChar(const ui::KeyEvent& event) {}

void PinField::InsertText(const std::u16string& text,
                          InsertTextCursorBehavior cursor_behavior) {}

bool PinField::IsTextEditCommandEnabled(ui::TextEditCommand command) const {
  return false;
}

void PinField::OnPaint(gfx::Canvas* canvas) {}

bool PinField::HandleKeyEvent(views::Textfield* sender,
                              const ui::KeyEvent& event) {
  return event.type() == ui::EventType::kKeyPressed ? key_.Run(event)
                                                    : holds_keys_.Run();
}

BEGIN_METADATA(PinField)
END_METADATA

}  // namespace pane
