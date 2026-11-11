// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_AUTOMATION_NATIVE_INPUT_MAC_H_
#define PANE_AUTOMATION_NATIVE_INPUT_MAC_H_

#include <CoreGraphics/CoreGraphics.h>
#include <stdint.h>

#include <string>

namespace views {
class Widget;
}

namespace pane::native_input {

// AppKit's events for the channel's keys (input_mac.cc), made as a keyboard's
// would be: their flags are CGEventFlags, whose bits NSEvent's modifier
// flags are.

// A key going down or up, typing `characters`, `ignoring` as its
// charactersIgnoringModifiers: posted to the app's queue, as the system hands
// it a key, or sent to the window itself (`to_window`).
void Key(views::Widget* widget,
         bool down,
         CGEventFlags flags,
         const std::string& characters,
         const std::string& ignoring,
         uint16_t code,
         bool to_window);

// A modifier key's change, `flags` the modifiers down after it, posted to the
// app's queue.
void FlagsChanged(views::Widget* widget, CGEventFlags flags, uint16_t code);

}  // namespace pane::native_input

#endif  // PANE_AUTOMATION_NATIVE_INPUT_MAC_H_
