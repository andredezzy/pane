// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_PIN_FIELD_H_
#define PANE_UI_DESIGN_PIN_FIELD_H_

#include <string>

#include "base/functional/callback.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"

namespace pane {

// What holds the keys while a PIN shows, read as the PIN's own field (ADR
// 0071): Chromium's own password field, named PIN, so a screen reader reads
// it as one, never the digits it holds. It holds none: a 0 for each digit
// typed, which it shows a screen reader as a bullet. With the PIN screen's
// panel holding the keys, Narrator spoke them; a view only claiming a text
// field's role it read nothing of, at the Lock nor walked (commit f475f04). It
// draws nothing, takes no press, offers no menu, and edits nothing: each key
// goes to the PIN screen, ahead of the field's own handling.
class PinField : public views::Textfield, public views::TextfieldController {
  METADATA_HEADER(PinField, views::Textfield)

 public:
  // `key` takes a key pressed and says whether it took it; `holds_keys` says
  // whether the PIN screen takes every key, as it does while a PIN shows.
  PinField(base::RepeatingCallback<bool(const ui::KeyEvent&)> key,
           base::RepeatingCallback<bool()> holds_keys);
  PinField(const PinField&) = delete;
  PinField& operator=(const PinField&) = delete;
  ~PinField() override;

  // `count` digits typed.
  void SetDigits(int count);

  // views::Textfield:
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
  void InsertChar(const ui::KeyEvent& event) override;
  void InsertText(const std::u16string& text,
                  InsertTextCursorBehavior cursor_behavior) override;
  bool IsTextEditCommandEnabled(ui::TextEditCommand command) const override;
  void OnPaint(gfx::Canvas* canvas) override;

  // views::TextfieldController: its keys, ahead of the field's own handling,
  // Textfield's own key handlers being final.
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& event) override;

 private:
  base::RepeatingCallback<bool(const ui::KeyEvent&)> key_;
  base::RepeatingCallback<bool()> holds_keys_;
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_PIN_FIELD_H_
