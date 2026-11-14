// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/commands.h"

#include "base/no_destructor.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace pane {

namespace {

constexpr int kCommand = ui::EF_COMMAND_DOWN;
constexpr int kShift = ui::EF_SHIFT_DOWN;
constexpr int kControl = ui::EF_CONTROL_DOWN;
// ⌥ on the Mac, Alt on Windows.
constexpr int kOption = ui::EF_ALT_DOWN;

// A command's key on each system; VKEY_UNKNOWN where it has none there.
struct Keys {
  ui::Accelerator mac;
  ui::Accelerator windows;
};

// The key most commands have: ⌘ on the Mac, Ctrl on Windows, with
// `modifiers`.
Keys Primary(ui::KeyboardCode key, int modifiers = ui::EF_NONE) {
  return {.mac = ui::Accelerator(key, kCommand | modifiers),
          .windows = ui::Accelerator(key, kControl | modifiers)};
}

// The same key on both: Ctrl-Tab, the switcher's.
Keys Both(ui::KeyboardCode key, int modifiers) {
  return {.mac = ui::Accelerator(key, modifiers),
          .windows = ui::Accelerator(key, modifiers)};
}

Keys MacOnly(ui::KeyboardCode key, int modifiers) {
  return {.mac = ui::Accelerator(key, modifiers)};
}

Keys WindowsOnly(ui::KeyboardCode key, int modifiers) {
  return {.windows = ui::Accelerator(key, modifiers)};
}

Keys Each(ui::Accelerator mac, ui::Accelerator windows) {
  return {.mac = mac, .windows = windows};
}

// Builds one system's menus from the table.
class Builder {
 public:
  explicit Builder(KeySystem system) : system_(system) {}

  MenuItem Item(Command command,
                std::u16string title,
                const Keys& keys = Keys()) const {
    return MenuItem{.command = command,
                    .title = std::move(title),
                    .accelerator = KeyOf(keys)};
  }

  // A key beside an item's, shown nowhere: none on a system without it.
  void AddHidden(std::vector<MenuItem>& items,
                 Command command,
                 std::u16string title,
                 const Keys& keys) const {
    MenuItem item = Item(command, std::move(title), keys);
    if (item.accelerator.key_code() == ui::VKEY_UNKNOWN) {
      return;
    }
    item.hidden = true;
    items.push_back(std::move(item));
  }

 private:
  const ui::Accelerator& KeyOf(const Keys& keys) const {
    return system_ == KeySystem::kMac ? keys.mac : keys.windows;
  }

  const KeySystem system_;
};

MenuItem Separator() {
  return MenuItem();
}

Menu MenuOf(std::u16string title, std::vector<MenuItem> items) {
  Menu menu;
  menu.title = std::move(title);
  menu.items = std::move(items);
  return menu;
}

// The Swift's MainMenu, item for item: its titles, its keys (an uppercase
// key there is Shift's: ⇧⌘T, ⇧⌘Z, ⇧⌘I), its separators; and what the user
// added from Chrome's: Find Next and Find Previous (⌘G, ⇧⌘G), and ⌥⌘I for
// Developer Tools beside the Swift's ⇧⌘I. On Windows, Ctrl where the Mac
// takes ⌘, and Chrome's own Windows keys where Ctrl alone would collide
// (⌃⌘F and ⌃⌘L are Ctrl+F's and Ctrl+L's) or where Windows has its own,
// as Chrome's accelerator table gives them there: F11 for Full Screen,
// Alt+Left and Alt+Right for Back and Forward, F12 beside Ctrl+Shift+I,
// Ctrl+Y for Redo, Ctrl+= for Zoom In with Ctrl++ and the numpad's keys
// beside the zooms, and Alt+F4 beside Ctrl+Q for Quit (ADR 0053); none for
// what is the Mac's alone (Hide, Hide Others, Minimize). Lock Pane is
// Ctrl+Shift+L there.
std::vector<Menu> MakeMenus(KeySystem system) {
  const Builder b(system);
  std::vector<Menu> menus;
  std::vector<MenuItem> app = {
      b.Item(Command::kAbout, u"About Pane"),
      Separator(),
      b.Item(Command::kCheckForUpdates, u"Check for Updates…"),
      Separator(),
      b.Item(Command::kSettings, u"Settings…", Primary(ui::VKEY_OEM_COMMA)),
      Separator(),
      // Paper 07.9: every profile's data closes, and the PIN screen shows.
      b.Item(Command::kLock, u"Lock Pane",
             Each(ui::Accelerator(ui::VKEY_L, kCommand | kControl),
                  ui::Accelerator(ui::VKEY_L, kControl | kShift))),
      Separator(),
      b.Item(Command::kHide, u"Hide Pane", MacOnly(ui::VKEY_H, kCommand)),
      b.Item(Command::kHideOthers, u"Hide Others",
             MacOnly(ui::VKEY_H, kCommand | kOption)),
      b.Item(Command::kShowAll, u"Show All"),
      Separator(),
      b.Item(Command::kQuit, u"Quit Pane", Primary(ui::VKEY_Q)),
  };
  // Windows' own key for closing an app's window, which quits Pane, as ⌘Q
  // does: asking first (ADR 0053).
  b.AddHidden(app, Command::kQuit, u"Quit Pane",
              WindowsOnly(ui::VKEY_F4, kOption));
  menus.push_back(MenuOf(u"Pane", std::move(app)));
  menus.push_back(
      MenuOf(u"File",
             {
                 b.Item(Command::kNewTab, u"New Tab", Primary(ui::VKEY_T)),
                 b.Item(Command::kCloseTab, u"Close Tab", Primary(ui::VKEY_W)),
                 b.Item(Command::kReopenClosedTab, u"Reopen Closed Tab",
                        Primary(ui::VKEY_T, kShift)),
                 Separator(),
                 b.Item(Command::kPrint, u"Print…", Primary(ui::VKEY_P)),
             }));
  menus.push_back(MenuOf(
      u"Edit",
      {
          b.Item(Command::kUndo, u"Undo", Primary(ui::VKEY_Z)),
          b.Item(Command::kRedo, u"Redo",
                 Each(ui::Accelerator(ui::VKEY_Z, kCommand | kShift),
                      ui::Accelerator(ui::VKEY_Y, kControl))),
          Separator(),
          b.Item(Command::kCut, u"Cut", Primary(ui::VKEY_X)),
          b.Item(Command::kCopy, u"Copy", Primary(ui::VKEY_C)),
          b.Item(Command::kPaste, u"Paste", Primary(ui::VKEY_V)),
          b.Item(Command::kSelectAll, u"Select All", Primary(ui::VKEY_A)),
      }));
  std::vector<MenuItem> view = {
      b.Item(Command::kFind, u"Find in Page", Primary(ui::VKEY_F)),
      b.Item(Command::kFindNext, u"Find Next", Primary(ui::VKEY_G)),
      b.Item(Command::kFindPrevious, u"Find Previous",
             Primary(ui::VKEY_G, kShift)),
      Separator(),
      b.Item(Command::kReload, u"Reload", Primary(ui::VKEY_R)),
      Separator(),
      b.Item(Command::kActualSize, u"Actual Size", Primary(ui::VKEY_0)),
      // ⌘+ on the Mac, which the keyboard types with Shift; Ctrl+= on
      // Windows, what people type there, as Chrome shows it.
      b.Item(Command::kZoomIn, u"Zoom In",
             Each(ui::Accelerator(ui::VKEY_OEM_PLUS, kCommand | kShift),
                  ui::Accelerator(ui::VKEY_OEM_PLUS, kControl))),
  };
  // The other's beside it, ⌘= and Ctrl++, and on Windows the numpad's +, as
  // Chrome's Windows table takes each.
  b.AddHidden(view, Command::kZoomIn, u"Zoom In",
              Each(ui::Accelerator(ui::VKEY_OEM_PLUS, kCommand),
                   ui::Accelerator(ui::VKEY_OEM_PLUS, kControl | kShift)));
  b.AddHidden(view, Command::kZoomIn, u"Zoom In",
              WindowsOnly(ui::VKEY_ADD, kControl));
  view.push_back(
      b.Item(Command::kZoomOut, u"Zoom Out", Primary(ui::VKEY_OEM_MINUS)));
  // Ctrl+Shift+- and the numpad's - on Windows, as Chrome takes them there.
  b.AddHidden(view, Command::kZoomOut, u"Zoom Out",
              WindowsOnly(ui::VKEY_OEM_MINUS, kControl | kShift));
  b.AddHidden(view, Command::kZoomOut, u"Zoom Out",
              WindowsOnly(ui::VKEY_SUBTRACT, kControl));
  view.push_back(Separator());
  // Exit Full Screen in full screen (CommandTitle).
  view.push_back(b.Item(Command::kFullScreen, u"Enter Full Screen",
                        Each(ui::Accelerator(ui::VKEY_F, kCommand | kControl),
                             ui::Accelerator(ui::VKEY_F11, ui::EF_NONE))));
  view.push_back(Separator());
  view.push_back(b.Item(Command::kDeveloperTools, u"Developer Tools",
                        Primary(ui::VKEY_I, kShift)));
  // Chrome's and Safari's key for the page's tools, its key alone: ⌥⌘I on
  // the Mac, F12 on Windows.
  b.AddHidden(view, Command::kDeveloperTools, u"Developer Tools",
              Each(ui::Accelerator(ui::VKEY_I, kCommand | kOption),
                   ui::Accelerator(ui::VKEY_F12, ui::EF_NONE)));
  menus.push_back(MenuOf(u"View", std::move(view)));
  menus.push_back(MenuOf(
      u"Navigate", {
                       b.Item(Command::kBack, u"Back",
                              Each(ui::Accelerator(ui::VKEY_OEM_4, kCommand),
                                   ui::Accelerator(ui::VKEY_LEFT, kOption))),
                       b.Item(Command::kForward, u"Forward",
                              Each(ui::Accelerator(ui::VKEY_OEM_6, kCommand),
                                   ui::Accelerator(ui::VKEY_RIGHT, kOption))),
                       Separator(),
                       b.Item(Command::kFocusAddress, u"Focus Address Bar",
                              Primary(ui::VKEY_L)),
                   }));
  std::vector<MenuItem> tabs = {
      b.Item(Command::kNextTab, u"Next Tab (Recent)",
             Both(ui::VKEY_TAB, kControl)),
      b.Item(Command::kPreviousTab, u"Previous Tab (Recent)",
             Both(ui::VKEY_TAB, kControl | kShift)),
      Separator(),
  };
  // ⌘1 to ⌘8 by place; ⌘9 the last, as Chrome's and Safari's (the user's
  // call, where the Swift's was the ninth).
  for (int number = 1; number <= 9; ++number) {
    tabs.push_back(b.Item(
        static_cast<Command>(static_cast<int>(Command::kTab1) + number - 1),
        number == 9
            ? std::u16string(u"Last Tab")
            : u"Tab " + std::u16string(1, static_cast<char16_t>(u'0' + number)),
        Primary(static_cast<ui::KeyboardCode>(ui::VKEY_0 + number))));
  }
  menus.push_back(MenuOf(u"Tab", std::move(tabs)));
  menus.push_back(MenuOf(
      u"Window",
      {
          b.Item(Command::kMinimize, u"Minimize",
                 MacOnly(ui::VKEY_M, kCommand)),
          b.Item(Command::kZoom, u"Zoom"),
          Separator(),
          // As Chrome's Window › Extensions. No key: pages use ⇧⌘E (Docs
          // centers text with it), and a menu's key would take it from them.
          b.Item(Command::kExtensions, u"Extensions"),
      }));
  return menus;
}

}  // namespace

Menu::Menu() = default;
Menu::Menu(const Menu&) = default;
Menu& Menu::operator=(const Menu&) = default;
Menu::~Menu() = default;

const std::vector<Menu>& MenuBarMenus() {
  static const base::NoDestructor<std::vector<Menu>> menus(
      MakeMenus(ThisKeySystem()));
  return *menus;
}

std::vector<Menu> MenusWithKeysOf(KeySystem system) {
  return MakeMenus(system);
}

bool IsPlatformCommand(Command command) {
  switch (command) {
    case Command::kAbout:
    case Command::kHide:
    case Command::kHideOthers:
    case Command::kShowAll:
    case Command::kUndo:
    case Command::kRedo:
    case Command::kCut:
    case Command::kCopy:
    case Command::kPaste:
    case Command::kSelectAll:
    case Command::kMinimize:
    case Command::kZoom:
      return true;
    default:
      return false;
  }
}

bool IsAppFirst(Command command, bool page_fullscreen) {
  if (page_fullscreen) {
    return command == Command::kQuit || command == Command::kFullScreen;
  }
  return command == Command::kQuit || command == Command::kNewTab ||
         command == Command::kCloseTab ||
         command == Command::kReopenClosedTab || command == Command::kNextTab ||
         command == Command::kPreviousTab;
}

std::optional<Command> CommandForAccelerator(
    const ui::Accelerator& accelerator) {
  for (const Menu& menu : MenuBarMenus()) {
    for (const MenuItem& item : menu.items) {
      if (item.command && item.accelerator.key_code() != ui::VKEY_UNKNOWN &&
          item.accelerator == accelerator) {
        return item.command;
      }
    }
  }
  return std::nullopt;
}

std::vector<ui::Accelerator> WindowAccelerators() {
  std::vector<ui::Accelerator> accelerators;
  for (const Menu& menu : MenuBarMenus()) {
    for (const MenuItem& item : menu.items) {
      if (item.command && !IsPlatformCommand(*item.command) &&
          item.accelerator.key_code() != ui::VKEY_UNKNOWN) {
        accelerators.push_back(item.accelerator);
      }
    }
  }
  return accelerators;
}

ui::Accelerator ShownAcceleratorOf(Command command) {
  for (const Menu& menu : MenuBarMenus()) {
    for (const MenuItem& item : menu.items) {
      if (item.command == command && !item.hidden) {
        return item.accelerator;
      }
    }
  }
  return ui::Accelerator();
}

int TabNumberOf(Command command) {
  const int number =
      static_cast<int>(command) - static_cast<int>(Command::kTab1) + 1;
  return number >= 1 && number <= 9 ? number : 0;
}

}  // namespace pane
