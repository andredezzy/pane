// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_COMMANDS_H_
#define PANE_UI_APP_COMMANDS_H_

#include <optional>
#include <string>
#include <vector>

#include "ui/base/accelerators/accelerator.h"

namespace pane {

// What the menu bar and the window's keys run: one command table for both
// (the Swift's MainMenu), each command once, in the order the menus show
// them.
enum class Command {
  // Pane: About Pane, Check for Updates…, Settings…, Lock Pane, Hide Pane,
  // Hide Others, Show All, and Quit Pane, which asks first (ADR 0053).
  kAbout = 1,
  kCheckForUpdates,
  kSettings,
  kLock,
  kHide,
  kHideOthers,
  kShowAll,
  kQuit,
  // File.
  kNewTab,
  kCloseTab,
  kReopenClosedTab,
  kPrint,
  // Edit, which acts on what has the keyboard: a field, or the page.
  kUndo,
  kRedo,
  kCut,
  kCopy,
  kPaste,
  kSelectAll,
  // View.
  kFind,
  kFindNext,
  kFindPrevious,
  kReload,
  kActualSize,
  kZoomIn,
  kZoomOut,
  kFullScreen,
  kDeveloperTools,
  // Navigate.
  kBack,
  kForward,
  kFocusAddress,
  // Tab: the switcher (Ctrl-Tab), then the active profile's tabs by place.
  kNextTab,
  kPreviousTab,
  kTab1,
  kTab2,
  kTab3,
  kTab4,
  kTab5,
  kTab6,
  kTab7,
  kTab8,
  kTab9,
  // Window: Minimize, Zoom, and the Extensions page.
  kMinimize,
  kZoom,
  kExtensions,
};

// The systems the table gives its keys for: the Mac's, which ⌘ leads, and
// Windows', which Ctrl leads where the Mac takes ⌘, Chrome's own Windows key
// standing where Ctrl alone would collide or Windows has its own (F11 for
// Full Screen, Alt+Left for Back, F12 for Developer Tools, Alt+F4 for Quit).
enum class KeySystem { kMac, kWindows };

// The system Pane runs on (commands_mac.cc, commands_win.cc).
KeySystem ThisKeySystem();

// An item of a menu: a command, its title, and the key that runs it on the
// system the menus are for (none for VKEY_UNKNOWN); a separator has no
// command.
struct MenuItem {
  std::optional<Command> command;
  std::u16string title;
  ui::Accelerator accelerator;
  // Shown nowhere, its key alone: ⌘= for Zoom In, beside ⌘+, as AppKit
  // matches a key as typed (ADR 0019); Windows' own keys beside Ctrl's.
  bool hidden = false;
};

struct Menu {
  Menu();
  Menu(const Menu&);
  Menu& operator=(const Menu&);
  ~Menu();

  std::u16string title;
  std::vector<MenuItem> items;
};

// The menu bar, in its order: Pane, File, Edit, View, Navigate, Tab, and
// Window, with this system's keys.
const std::vector<Menu>& MenuBarMenus();
// The same menus with `system`'s keys: one table, a column of keys for each
// system (ADR 0019). A key a system has none of leaves its hidden item out.
std::vector<Menu> MenusWithKeysOf(KeySystem system);

// The platform's own items, which the menu bar alone shows and the
// platform runs and enables (AppKit's actions, through the responder chain on
// the Mac), as the Swift's did: About, Hide, Hide Others, Show All, the Edit
// menu, Minimize, and Zoom. The window registers none of their keys: a field
// or the page takes its own Cut, Copy, and Paste.
bool IsPlatformCommand(Command command);

// The window runs it ahead of the page, which never sees its key: what
// Chrome reserves (`kReservedCommandIds`, ADR 0019), so no page can keep a
// user in it: Quit, New Tab, Close Tab, Reopen Closed Tab, and the
// switcher's Ctrl-Tab. While the page fills the screen, only Quit and Full
// Screen, as Chrome's page fullscreen keeps them. Every other key goes to the
// page first, then to the window's table.
bool IsAppFirst(Command command, bool page_fullscreen);

// The command a key runs, of the window's or the platform's; none for a key
// the table doesn't hold.
std::optional<Command> CommandForAccelerator(
    const ui::Accelerator& accelerator);

// The keys the window registers with its FocusManager: the table's, but the
// platform's own.
std::vector<ui::Accelerator> WindowAccelerators();

// The key a command's item shows, on this system; empty (VKEY_UNKNOWN) for
// none: what Chrome's own menus show beside the command (Exit Full Screen in
// the page's menu).
ui::Accelerator ShownAcceleratorOf(Command command);

// ⌘1 to ⌘9's tab: 1 to 9; 0 for any other command.
int TabNumberOf(Command command);

// What runs a command and says whether it may run now: the app, for the
// menu bar and the window's keys alike.
class CommandRunner {
 public:
  virtual ~CommandRunner() = default;

  virtual bool IsCommandEnabled(Command command) = 0;
  virtual void RunCommand(Command command) = 0;
  // The title its item shows now, where it follows the app (Enter or Exit
  // Full Screen); none keeps the table's.
  virtual std::optional<std::u16string> CommandTitle(Command command) = 0;
};

}  // namespace pane

#endif  // PANE_UI_APP_COMMANDS_H_
