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
constexpr int kOption = ui::EF_ALT_DOWN;

MenuItem Item(Command command,
              std::u16string title,
              ui::KeyboardCode key = ui::VKEY_UNKNOWN,
              int modifiers = ui::EF_NONE) {
  return MenuItem{.command = command,
                  .title = std::move(title),
                  .accelerator = ui::Accelerator(key, modifiers)};
}

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
// key there is Shift's: ⇧⌘T, ⇧⌘Z, ⇧⌘I), its separators.
std::vector<Menu> MakeMenus() {
  std::vector<Menu> menus;
  menus.push_back(MenuOf(
      u"Pane",
      {
          Item(Command::kAbout, u"About Pane"),
          Separator(),
          Item(Command::kCheckForUpdates, u"Check for Updates…"),
          Separator(),
          Item(Command::kSettings, u"Settings…", ui::VKEY_OEM_COMMA, kCommand),
          Separator(),
          // Paper 07.9: every profile's data closes, and the PIN screen
          // shows.
          Item(Command::kLock, u"Lock Pane", ui::VKEY_L, kCommand | kControl),
          Separator(),
          Item(Command::kHide, u"Hide Pane", ui::VKEY_H, kCommand),
          Item(Command::kHideOthers, u"Hide Others", ui::VKEY_H,
               kCommand | kOption),
          Item(Command::kShowAll, u"Show All"),
          Separator(),
          Item(Command::kQuit, u"Quit Pane", ui::VKEY_Q, kCommand),
      }));
  menus.push_back(MenuOf(
      u"File", {
                   Item(Command::kNewTab, u"New Tab", ui::VKEY_T, kCommand),
                   Item(Command::kCloseTab, u"Close Tab", ui::VKEY_W, kCommand),
                   Item(Command::kReopenClosedTab, u"Reopen Closed Tab",
                        ui::VKEY_T, kCommand | kShift),
                   Separator(),
                   Item(Command::kPrint, u"Print…", ui::VKEY_P, kCommand),
               }));
  menus.push_back(
      MenuOf(u"Edit",
             {
                 Item(Command::kUndo, u"Undo", ui::VKEY_Z, kCommand),
                 Item(Command::kRedo, u"Redo", ui::VKEY_Z, kCommand | kShift),
                 Separator(),
                 Item(Command::kCut, u"Cut", ui::VKEY_X, kCommand),
                 Item(Command::kCopy, u"Copy", ui::VKEY_C, kCommand),
                 Item(Command::kPaste, u"Paste", ui::VKEY_V, kCommand),
                 Item(Command::kSelectAll, u"Select All", ui::VKEY_A, kCommand),
             }));
  MenuItem zoom_in_typed =
      Item(Command::kZoomIn, u"Zoom In", ui::VKEY_OEM_PLUS, kCommand);
  zoom_in_typed.hidden = true;
  menus.push_back(MenuOf(
      u"View",
      {
          Item(Command::kFind, u"Find in Page", ui::VKEY_F, kCommand),
          Separator(),
          Item(Command::kReload, u"Reload", ui::VKEY_R, kCommand),
          Separator(),
          Item(Command::kActualSize, u"Actual Size", ui::VKEY_0, kCommand),
          // ⌘+, which the keyboard types with Shift.
          Item(Command::kZoomIn, u"Zoom In", ui::VKEY_OEM_PLUS,
               kCommand | kShift),
          zoom_in_typed,
          Item(Command::kZoomOut, u"Zoom Out", ui::VKEY_OEM_MINUS, kCommand),
          Separator(),
          // Exit Full Screen in full screen (CommandTitle).
          Item(Command::kFullScreen, u"Enter Full Screen", ui::VKEY_F,
               kCommand | kControl),
          Separator(),
          Item(Command::kDeveloperTools, u"Developer Tools", ui::VKEY_I,
               kCommand | kShift),
      }));
  menus.push_back(
      MenuOf(u"Navigate",
             {
                 Item(Command::kBack, u"Back", ui::VKEY_OEM_4, kCommand),
                 Item(Command::kForward, u"Forward", ui::VKEY_OEM_6, kCommand),
                 Separator(),
                 Item(Command::kFocusAddress, u"Focus Address Bar", ui::VKEY_L,
                      kCommand),
             }));
  std::vector<MenuItem> tabs = {
      Item(Command::kNextTab, u"Next Tab (Recent)", ui::VKEY_TAB, kControl),
      Item(Command::kPreviousTab, u"Previous Tab (Recent)", ui::VKEY_TAB,
           kControl | kShift),
      Separator(),
  };
  // ⌘1 to ⌘8 by place; ⌘9 the last, as Chrome's and Safari's (the user's
  // call, where the Swift's was the ninth).
  for (int number = 1; number <= 9; ++number) {
    tabs.push_back(Item(
        static_cast<Command>(static_cast<int>(Command::kTab1) + number - 1),
        number == 9
            ? std::u16string(u"Last Tab")
            : u"Tab " + std::u16string(1, static_cast<char16_t>(u'0' + number)),
        static_cast<ui::KeyboardCode>(ui::VKEY_0 + number), kCommand));
  }
  menus.push_back(MenuOf(u"Tab", std::move(tabs)));
  menus.push_back(MenuOf(
      u"Window",
      {
          Item(Command::kMinimize, u"Minimize", ui::VKEY_M, kCommand),
          Item(Command::kZoom, u"Zoom"),
          Separator(),
          // As Chrome's Window › Extensions. No key: pages use ⇧⌘E (Docs
          // centers text with it), and a menu's key would take it from them.
          Item(Command::kExtensions, u"Extensions"),
      }));
  return menus;
}

}  // namespace

Menu::Menu() = default;
Menu::Menu(const Menu&) = default;
Menu& Menu::operator=(const Menu&) = default;
Menu::~Menu() = default;

const std::vector<Menu>& MenuBarMenus() {
  static const base::NoDestructor<std::vector<Menu>> menus(MakeMenus());
  return *menus;
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

int TabNumberOf(Command command) {
  const int number =
      static_cast<int>(command) - static_cast<int>(Command::kTab1) + 1;
  return number >= 1 && number <= 9 ? number : 0;
}

std::optional<std::u16string> CommandRunner::CommandTitle(Command command) {
  return std::nullopt;
}

}  // namespace pane
