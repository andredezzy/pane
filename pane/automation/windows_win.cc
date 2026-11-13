// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/windows.h"

#include <windows.h>

#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "pane/automation/file_panels.h"
#include "pane/ui/extensions/action_popup.h"
#include "pane/ui/page/devtools_window.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/display/win/screen_win.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_win.h"
#include "ui/views/widget/widget.h"
#include "ui/views/win/hwnd_util.h"

namespace pane {

namespace {

std::string ClassOf(HWND hwnd) {
  wchar_t name[256] = {};
  ::GetClassNameW(hwnd, name, std::size(name));
  return base::WideToUTF8(name);
}

// The process's visible top-level windows, front to back, but the shadows
// Windows draws beside a popup of the process's in windows of their own
// ("SysShadow"), which are a part of that popup, as a shadow is of a Mac's.
std::vector<HWND> VisibleWindows() {
  std::vector<HWND> windows;
  ::EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        DWORD pid = 0;
        ::GetWindowThreadProcessId(hwnd, &pid);
        if (pid == ::GetCurrentProcessId() && ::IsWindowVisible(hwnd) &&
            ClassOf(hwnd) != "SysShadow") {
          reinterpret_cast<std::vector<HWND>*>(param)->push_back(hwnd);
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&windows));
  return windows;
}

// The Views widget a window of the process is, if it is one.
views::Widget* WidgetOf(HWND hwnd) {
  aura::Window* content =
      views::DesktopWindowTreeHostWin::GetContentWindowForHWND(hwnd);
  return content ? views::Widget::GetWidgetForNativeWindow(content) : nullptr;
}

// A window's name: its widget's, or, for a window of aura's whose widget
// isn't there yet or anymore (one being made or closed), its root window's,
// which takes the widget's name as it is made (DesktopNativeWidgetAura);
// else Windows' class of it.
std::string NameOf(HWND hwnd, views::Widget* widget) {
  if (widget && !widget->GetName().empty()) {
    return widget->GetName();
  }
  if (aura::WindowTreeHost* host =
          aura::WindowTreeHost::GetForAcceleratedWidget(hwnd);
      host && !host->window()->GetName().empty()) {
    return std::string(host->window()->GetName());
  }
  return ClassOf(hwnd);
}

// A window's text as Windows keeps it, asking the window nothing:
// GetWindowText sends a window of another thread a message and waits for
// it, and the system's dialogs run on threads of their own, which can wait
// on this one.
std::string TextOf(HWND hwnd) {
  wchar_t text[512] = {};
  return base::WideToUTF8(std::wstring_view(
      text, ::InternalGetWindowText(hwnd, text, std::size(text))));
}

HWND MainWindow(views::Widget* main) {
  return main && !main->IsClosed() ? views::HWNDForWidget(main) : nullptr;
}

// The dialog the system shows over Pane's window in Pane's process: a dialog
// of Windows' own (its class "#32770"), owned by the window, as the open and
// save dialogs are, and the print dialog before Windows 11's.
HWND SystemDialogOver(HWND main) {
  if (!main) {
    return nullptr;
  }
  for (HWND hwnd : VisibleWindows()) {
    if (ClassOf(hwnd) == "#32770" && ::GetWindow(hwnd, GW_OWNER) == main) {
      return hwnd;
    }
  }
  return nullptr;
}

// Windows 11's print dialog, which the shell draws in a process of its own:
// its frame there (an ApplicationFrameWindow, "Pane - Print"), while a window
// of this process is disabled under it, owned by the shell's
// ProxyModalWindow, which ties the two; else none.
HWND ShellDialog() {
  struct Found {
    DWORD shell = 0;
    HWND frame = nullptr;
  } found;
  ::EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        const HWND owner = ::GetWindow(hwnd, GW_OWNER);
        DWORD owner_pid = 0;
        if (owner) {
          ::GetWindowThreadProcessId(owner, &owner_pid);
        }
        if (owner_pid != ::GetCurrentProcessId() || ::IsWindowEnabled(owner) ||
            ClassOf(hwnd) != "ProxyModalWindow") {
          return TRUE;
        }
        ::GetWindowThreadProcessId(hwnd,
                                   &reinterpret_cast<Found*>(param)->shell);
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&found));
  if (!found.shell) {
    return nullptr;
  }
  // The shell's frame in front of the others, once the dialog in it is made,
  // its title with it: a frame closed sooner once left the page's print()
  // waiting on a dialog that had gone.
  ::EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        Found* found = reinterpret_cast<Found*>(param);
        DWORD pid = 0;
        ::GetWindowThreadProcessId(hwnd, &pid);
        if (pid != found->shell || !::IsWindowVisible(hwnd) ||
            ClassOf(hwnd) != "ApplicationFrameWindow" || TextOf(hwnd).empty()) {
          return TRUE;
        }
        found->frame = hwnd;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&found));
  return found.frame;
}

// A dialog's buttons, in the order Windows keeps its children, each with its
// title as it shows: "&Print" reads "Print".
std::vector<std::pair<HWND, std::string>> ButtonsOf(HWND dialog) {
  std::vector<std::pair<HWND, std::string>> buttons;
  ::EnumChildWindows(
      dialog,
      [](HWND child, LPARAM param) -> BOOL {
        if (ClassOf(child) == "Button" && ::IsWindowVisible(child)) {
          std::string title;
          base::RemoveChars(TextOf(child), "&", &title);
          reinterpret_cast<std::vector<std::pair<HWND, std::string>>*>(param)
              ->emplace_back(child, title);
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&buttons));
  return buttons;
}

}  // namespace

base::ListValue NativeWindows(views::Widget* main) {
  const HWND pane = MainWindow(main);
  const HWND modal = SystemDialogOver(pane);
  base::ListValue windows;
  for (HWND hwnd : VisibleWindows()) {
    const std::string name = NameOf(hwnd, WidgetOf(hwnd));
    const char* role = "other";
    if (hwnd == pane) {
      role = "main";
    } else if (hwnd == modal) {
      // The open or save dialog, as the Mac's panel is the window's sheet,
      // while one is asked for (file_panels.h); else the print dialog,
      // app-modal.
      role = FilePanelShown().is_none() ? "modal" : "sheet";
    } else if (name == kDevToolsWindowName) {
      role = "devtools";
    } else if (name == kActionPopupWindowName) {
      // An action's popup, as the Swift's NSPopover's window was named.
      role = "popover";
    }
    RECT pixels = {};
    ::GetWindowRect(hwnd, &pixels);
    const gfx::Rect frame =
        display::win::GetScreenWin()->ScreenToDIPRect(hwnd, gfx::Rect(pixels));
    windows.Append(
        base::DictValue()
            .Set("role", role)
            // A Views widget by its name (Views' menus are "MenuHost"),
            // another window by its class.
            .Set("class", name)
            .Set("title", TextOf(hwnd))
            // The window's handle: what a capture of it takes.
            .Set("number", static_cast<int>(reinterpret_cast<intptr_t>(hwnd)))
            // In DIPs from the screen's top left, as the Mac's
            // NSStringFromRect writes a frame.
            .Set("frame",
                 base::StringPrintf("{{%d, %d}, {%d, %d}}", frame.x(),
                                    frame.y(), frame.width(), frame.height())));
  }
  return windows;
}

// The system's alert (ShowSystemAlert) shows nothing on Windows yet, W5's:
// no sheet shows on the window.
base::Value SheetAlert(views::Widget* main) {
  return base::Value();
}

bool PressSheetButton(views::Widget* main, std::string_view title) {
  if (const HWND dialog = SystemDialogOver(MainWindow(main))) {
    for (const auto& [button, text] : ButtonsOf(dialog)) {
      if (text == title) {
        // Posted: the dialog's thread clicks it, waiting on no one here.
        ::PostMessage(button, BM_CLICK, 0, 0);
        return true;
      }
    }
    return false;
  }
  // Windows 11's print dialog: its buttons are XAML's, which no window
  // message presses, so Cancel is its close button's, which cancels it as
  // Cancel does; the others wait on UI Automation.
  if (const HWND frame = ShellDialog(); frame && title == "Cancel") {
    ::PostMessage(frame, WM_CLOSE, 0, 0);
    return true;
  }
  return false;
}

// The print dialog, app-modal: a dialog of the system's whose owner it
// disables, while no open or save dialog is asked for; on Windows 11 the
// shell's, its buttons, XAML's, listed by no window.
base::Value ModalPanel() {
  if (!FilePanelShown().is_none()) {
    return base::Value();
  }
  for (HWND hwnd : VisibleWindows()) {
    const HWND owner = ::GetWindow(hwnd, GW_OWNER);
    if (ClassOf(hwnd) != "#32770" || !owner || ::IsWindowEnabled(owner)) {
      continue;
    }
    base::ListValue titles;
    for (const auto& [button, text] : ButtonsOf(hwnd)) {
      titles.Append(text);
    }
    return base::Value(base::DictValue()
                           .Set("title", TextOf(hwnd))
                           .Set("buttons", std::move(titles)));
  }
  if (const HWND frame = ShellDialog()) {
    return base::Value(base::DictValue()
                           .Set("title", TextOf(frame))
                           .Set("buttons", base::ListValue()));
  }
  return base::Value();
}

bool ResignKey(views::Widget* main) {
  const HWND hwnd = MainWindow(main);
  if (!hwnd) {
    return false;
  }
  // As Windows tells a window it lost the activation: its handler tells
  // Views, which keeps the focus for when it is active again.
  ::SendMessage(hwnd, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, FALSE), 0);
  return true;
}

}  // namespace pane
