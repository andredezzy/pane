// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/input.h"

#include <windows.h>

#include <algorithm>
#include <optional>
#include <tuple>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/widget/widget.h"
#include "ui/views/win/hwnd_util.h"

namespace pane {

namespace {

// The input reaches the window as Windows' own does past its message queue:
// each key or mouse event made as the window's handler makes it from a
// message (HWNDMessageHandler, then DesktopWindowTreeHostWin), with the
// modifiers a real keyboard would hold, and handed to the window's event sink,
// aura's dispatcher: the input method, Views' accelerators and focus, the
// page. Views' own test helper builds Windows' keys the same way
// (ui::test::EventGenerator). Nothing goes through the system's input, which
// reaches whatever window is in front (SendInput), so a run behind the
// user's windows works (ADR 0012), and a press takes no capture of the
// user's real pointer: Views captures only while a real button is down.

// The keys scenarios type, by Windows' virtual key codes on the US layout.
std::optional<ui::KeyboardCode> KeyCodeOf(const std::string& key) {
  if (key.size() != 1) {
    return std::nullopt;
  }
  const char typed = key[0];
  if (typed >= 'a' && typed <= 'z') {
    return static_cast<ui::KeyboardCode>(ui::VKEY_A + (typed - 'a'));
  }
  if (typed >= '0' && typed <= '9') {
    return static_cast<ui::KeyboardCode>(ui::VKEY_0 + (typed - '0'));
  }
  switch (typed) {
    case '=':
      return ui::VKEY_OEM_PLUS;
    case '-':
      return ui::VKEY_OEM_MINUS;
    case '[':
      return ui::VKEY_OEM_4;
    case ']':
      return ui::VKEY_OEM_6;
    case ',':
      return ui::VKEY_OEM_COMMA;
    case ' ':
      return ui::VKEY_SPACE;
    case '\t':
      return ui::VKEY_TAB;
    case '\r':
      return ui::VKEY_RETURN;
    case '\x1b':
      return ui::VKEY_ESCAPE;
    // Delete, back a character, as the Mac's key of that name types it.
    case '\x7f':
      return ui::VKEY_BACK;
  }
  return std::nullopt;
}

// Pane's names of the modifiers, as event flags: "command" is the Windows
// key, Chromium's EF_COMMAND_DOWN there.
int FlagsOf(const std::vector<std::string>& modifiers) {
  int flags = ui::EF_NONE;
  for (const std::string& name : modifiers) {
    if (name == "command") {
      flags |= ui::EF_COMMAND_DOWN;
    } else if (name == "shift") {
      flags |= ui::EF_SHIFT_DOWN;
    } else if (name == "control") {
      flags |= ui::EF_CONTROL_DOWN;
    } else if (name == "option") {
      flags |= ui::EF_ALT_DOWN;
    } else if (name == "capsLock") {
      flags |= ui::EF_CAPS_LOCK_ON;
    }
  }
  return flags;
}

// The key a modifier is.
ui::KeyboardCode ModifierKeyCode(const std::string& modifier) {
  if (modifier == "command") {
    return ui::VKEY_LWIN;
  }
  if (modifier == "shift") {
    return ui::VKEY_SHIFT;
  }
  if (modifier == "option") {
    return ui::VKEY_MENU;
  }
  if (modifier == "capsLock") {
    return ui::VKEY_CAPITAL;
  }
  return ui::VKEY_CONTROL;
}

// Alt without Ctrl makes a key a system key (WM_SYSKEYDOWN, WM_SYSCHAR); with
// Ctrl it is AltGr's, a plain one.
bool IsSystemKey(int flags) {
  return (flags & ui::EF_ALT_DOWN) && !(flags & ui::EF_CONTROL_DOWN);
}

// A key's message as Windows posts it: the key's scan code, a repeat count of
// one, and on its release the transition's bits.
CHROME_MSG KeyMessage(HWND hwnd, bool down, ui::KeyboardCode code, int flags) {
  const bool system = IsSystemKey(flags);
  LPARAM l_param = 1 | ui::GetLParamFromScanCode(
                           ui::KeycodeConverter::DomCodeToNativeKeycode(
                               ui::UsLayoutKeyboardCodeToDomCode(code)));
  if (system) {
    l_param |= 1 << 29;
  }
  if (!down) {
    l_param |= (1 << 30) | (1u << 31);
  }
  CHROME_MSG message = {};
  message.hwnd = hwnd;
  message.message = down ? (system ? WM_SYSKEYDOWN : WM_KEYDOWN)
                         : (system ? WM_SYSKEYUP : WM_KEYUP);
  message.wParam = static_cast<WPARAM>(code);
  message.lParam = l_param;
  message.time = ::GetTickCount();
  return message;
}

// The character Windows' TranslateMessage makes of a key on the US layout: a
// control character under Ctrl, none for a key that types none.
char16_t CharacterOf(ui::KeyboardCode code, int flags) {
  const ui::DomCode dom_code = ui::UsLayoutKeyboardCodeToDomCode(code);
  ui::DomKey key;
  ui::KeyboardCode control_code;
  if (flags & ui::EF_CONTROL_DOWN) {
    return ui::DomCodeToControlCharacter(dom_code, flags, &key, &control_code)
               ? static_cast<char16_t>(key.ToCharacter())
               : 0;
  }
  return ui::DomCodeToUsLayoutCharacter(dom_code, flags);
}

// The window's own dispatch: its host's event sink, as its handler hands
// Windows' messages to it.
void Dispatch(views::Widget* widget, ui::Event* event) {
  aura::Window* window =
      widget && !widget->IsClosed() ? widget->GetNativeWindow() : nullptr;
  if (aura::WindowTreeHost* host = window ? window->GetHost() : nullptr) {
    std::ignore = host->SendEventToSink(event);
  }
}

// A key typed: its press, the character the press posts, which the input
// method takes with it as it takes a real one (InputMethodWinBase's
// DispatchKeyEvent, which drops it when the press was handled), and its
// release. False when the key has no code.
bool Type(base::WeakPtr<views::Widget> widget,
          const std::string& key,
          int flags) {
  const std::optional<ui::KeyboardCode> code = KeyCodeOf(key);
  if (!code) {
    return false;
  }
  if (!widget || widget->IsClosed()) {
    return true;
  }
  const HWND hwnd = views::HWNDForWidget(widget.get());
  const CHROME_MSG press = KeyMessage(hwnd, true, *code, flags);
  if (const char16_t character = CharacterOf(*code, flags)) {
    ::PostMessage(hwnd, IsSystemKey(flags) ? WM_SYSCHAR : WM_CHAR, character,
                  press.lParam);
  }
  ui::KeyEvent pressed(press, flags);
  Dispatch(widget.get(), &pressed);
  if (widget) {
    ui::KeyEvent released(KeyMessage(hwnd, false, *code, flags), flags);
    Dispatch(widget.get(), &released);
  }
  return true;
}

// What the channel holds down, as fingers would: a key posted with modifiers
// leaves them down (a Ctrl-Tab's Ctrl), and releaseModifiers lets each go,
// the last pressed first.
std::vector<std::string>& Held() {
  static base::NoDestructor<std::vector<std::string>> held;
  return *held;
}

// The modifiers going down or up to match `modifiers`, each as its own key's
// press or release, the flags of what is down after it.
void HoldModifiers(base::WeakPtr<views::Widget> widget,
                   const std::vector<std::string>& modifiers) {
  std::vector<std::string>& held = Held();
  const auto change = [&](const std::string& modifier, bool down) {
    if (!widget || widget->IsClosed()) {
      return;
    }
    ui::KeyEvent event(KeyMessage(views::HWNDForWidget(widget.get()), down,
                                  ModifierKeyCode(modifier), FlagsOf(held)),
                       FlagsOf(held));
    Dispatch(widget.get(), &event);
  };
  for (size_t i = held.size(); i-- > 0;) {
    if (!std::ranges::contains(modifiers, held[i])) {
      const std::string modifier = held[i];
      held.erase(held.begin() + i);
      change(modifier, false);
    }
  }
  for (const std::string& modifier : modifiers) {
    if (!std::ranges::contains(held, modifier)) {
      held.push_back(modifier);
      change(modifier, true);
    }
  }
}

// After what the channel did before it, as the Mac's queue runs posted
// events in order: the app's own task queue.
void Post(base::OnceClosure input) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                           std::move(input));
}

// A point of the window (from its top left, in DIPs) in its host's pixels,
// as a message's point is: the host turns it back into DIPs.
gfx::PointF PixelsOf(views::Widget* widget, int x, int y) {
  gfx::PointF point(x, y);
  if (aura::WindowTreeHost* host = widget->GetNativeWindow()->GetHost()) {
    host->ConvertDIPToPixels(&point);
  }
  return point;
}

void Move(views::Widget* widget, int x, int y) {
  const gfx::PointF at = PixelsOf(widget, x, y);
  ui::MouseEvent moved(ui::EventType::kMouseMoved, at, at,
                       ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE);
  Dispatch(widget, &moved);
}

}  // namespace

bool PostKey(views::Widget* widget,
             const std::string& key,
             const std::vector<std::string>& modifiers) {
  if (!KeyCodeOf(key)) {
    return false;
  }
  Post(base::BindOnce(
      [](base::WeakPtr<views::Widget> widget, const std::string& key,
         const std::vector<std::string>& modifiers) {
        const int flags = FlagsOf(modifiers);
        if (flags) {
          HoldModifiers(widget, modifiers);
        }
        Type(widget, key, flags);
      },
      widget ? widget->GetWeakPtr() : nullptr, key, modifiers));
  return true;
}

void PostClick(views::Widget* widget, int x, int y, int clicks) {
  Post(base::BindOnce(
      [](base::WeakPtr<views::Widget> widget, int x, int y, int clicks) {
        if (!widget || widget->IsClosed()) {
          return;
        }
        // The pointer gets there before it presses, as a hand's does.
        Move(widget.get(), x, y);
        const gfx::PointF at = PixelsOf(widget.get(), x, y);
        for (int count = 1; count <= std::max(clicks, 1); ++count) {
          for (const ui::EventType type :
               {ui::EventType::kMousePressed, ui::EventType::kMouseReleased}) {
            if (!widget || widget->IsClosed()) {
              return;
            }
            ui::MouseEvent event(type, at, at, ui::EventTimeForNow(),
                                 ui::EF_LEFT_MOUSE_BUTTON,
                                 ui::EF_LEFT_MOUSE_BUTTON);
            event.SetClickCount(count);
            Dispatch(widget.get(), &event);
          }
        }
      },
      widget ? widget->GetWeakPtr() : nullptr, x, y, clicks));
}

void PostMove(views::Widget* widget, int x, int y) {
  if (widget && !widget->IsClosed()) {
    Move(widget, x, y);
  }
}

void PostScroll(views::Widget* widget, int x, int y, double dy) {
  if (!widget || widget->IsClosed()) {
    return;
  }
  const gfx::PointF at = PixelsOf(widget, x, y);
  ui::MouseWheelEvent wheel(gfx::Vector2d(0, static_cast<int>(dy)),
                            gfx::ToRoundedPoint(at), gfx::ToRoundedPoint(at),
                            ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE);
  Dispatch(widget, &wheel);
}

bool SendKeyToWindow(views::Widget* widget,
                     const std::string& key,
                     const std::vector<std::string>& modifiers) {
  return Type(widget ? widget->GetWeakPtr() : nullptr, key, FlagsOf(modifiers));
}

void PostModifiers(views::Widget* widget,
                   const std::vector<std::string>& modifiers) {
  Post(base::BindOnce(&HoldModifiers, widget ? widget->GetWeakPtr() : nullptr,
                      modifiers));
}

}  // namespace pane
