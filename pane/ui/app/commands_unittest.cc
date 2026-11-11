// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/commands.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "pane/ui/app/menu_bar.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/models/menu_model.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace pane {

namespace {

// A key as the Swift's menu showed it: ⌃⌥⇧⌘, then the key.
std::string KeyOf(const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_UNKNOWN) {
    return std::string();
  }
  std::string key;
  if (accelerator.IsCtrlDown()) {
    key += "⌃";
  }
  if (accelerator.IsAltDown()) {
    key += "⌥";
  }
  if (accelerator.IsShiftDown()) {
    key += "⇧";
  }
  if (accelerator.IsCmdDown()) {
    key += "⌘";
  }
  switch (accelerator.key_code()) {
    case ui::VKEY_OEM_COMMA:
      return key + ",";
    case ui::VKEY_OEM_PLUS:
      // ⇧= is the keyboard's +.
      return accelerator.IsShiftDown() ? key.erase(key.find("⇧"), 3) + "+"
                                       : key + "=";
    case ui::VKEY_OEM_MINUS:
      return key + "-";
    case ui::VKEY_OEM_4:
      return key + "[";
    case ui::VKEY_OEM_6:
      return key + "]";
    case ui::VKEY_TAB:
      return key + "⇥";
    default:
      return key + static_cast<char>(accelerator.key_code());
  }
}

// Each menu's items as "Title Key", separators as "-", hidden ones marked.
std::vector<std::string> Lines(const Menu& menu) {
  std::vector<std::string> lines;
  for (const MenuItem& item : menu.items) {
    if (!item.command) {
      lines.push_back("-");
      continue;
    }
    std::string line = base::UTF16ToUTF8(item.title);
    if (const std::string key = KeyOf(item.accelerator); !key.empty()) {
      line += " " + key;
    }
    if (item.hidden) {
      line += " (hidden)";
    }
    lines.push_back(line);
  }
  return lines;
}

class FakeRunner : public CommandRunner {
 public:
  bool IsCommandEnabled(Command command) override {
    return command != Command::kLock;
  }
  void RunCommand(Command command) override { ran.push_back(command); }
  std::optional<std::u16string> CommandTitle(Command command) override {
    if (command == Command::kFullScreen) {
      return full_screen ? u"Exit Full Screen" : u"Enter Full Screen";
    }
    return std::nullopt;
  }

  bool full_screen = false;
  std::vector<Command> ran;
};

}  // namespace

// The Swift's MainMenu, item for item: every menu, title, key, and
// separator, ⌘= Zoom In hidden under ⌘+'s.
TEST(CommandsTest, MenusAreTheSwifts) {
  const std::vector<std::pair<std::string, std::vector<std::string>>> swift = {
      {"Pane",
       {"About Pane", "-", "Check for Updates…", "-", "Settings… ⌘,", "-",
        "Lock Pane ⌃⌘L", "-", "Hide Pane ⌘H", "Hide Others ⌥⌘H", "Show All",
        "-", "Quit Pane ⌘Q"}},
      {"File",
       {"New Tab ⌘T", "Close Tab ⌘W", "Reopen Closed Tab ⇧⌘T", "-",
        "Print… ⌘P"}},
      {"Edit",
       {"Undo ⌘Z", "Redo ⇧⌘Z", "-", "Cut ⌘X", "Copy ⌘C", "Paste ⌘V",
        "Select All ⌘A"}},
      {"View",
       {"Find in Page ⌘F", "Find Next ⌘G", "Find Previous ⇧⌘G", "-",
        "Reload ⌘R", "-", "Actual Size ⌘0", "Zoom In ⌘+", "Zoom In ⌘= (hidden)",
        "Zoom Out ⌘-", "-", "Enter Full Screen ⌃⌘F", "-", "Developer Tools ⇧⌘I",
        "Developer Tools ⌥⌘I (hidden)"}},
      {"Navigate", {"Back ⌘[", "Forward ⌘]", "-", "Focus Address Bar ⌘L"}},
      {"Tab",
       {"Next Tab (Recent) ⌃⇥", "Previous Tab (Recent) ⌃⇧⇥", "-", "Tab 1 ⌘1",
        "Tab 2 ⌘2", "Tab 3 ⌘3", "Tab 4 ⌘4", "Tab 5 ⌘5", "Tab 6 ⌘6", "Tab 7 ⌘7",
        "Tab 8 ⌘8", "Last Tab ⌘9"}},
      {"Window", {"Minimize ⌘M", "Zoom", "-", "Extensions"}},
  };
  const std::vector<Menu>& menus = MenuBarMenus();
  ASSERT_EQ(menus.size(), swift.size());
  for (size_t index = 0; index < menus.size(); ++index) {
    EXPECT_EQ(base::UTF16ToUTF8(menus[index].title), swift[index].first);
    EXPECT_EQ(Lines(menus[index]), swift[index].second) << swift[index].first;
  }
}

// A key runs one command: no two items hold the same one.
TEST(CommandsTest, EachKeyRunsOneCommand) {
  std::set<ui::Accelerator> seen;
  for (const Menu& menu : MenuBarMenus()) {
    for (const MenuItem& item : menu.items) {
      if (item.command && item.accelerator.key_code() != ui::VKEY_UNKNOWN) {
        EXPECT_TRUE(seen.insert(item.accelerator).second)
            << base::UTF16ToUTF8(item.title);
        EXPECT_EQ(CommandForAccelerator(item.accelerator), item.command);
      }
    }
  }
  // Caps Lock changes nothing: ⌘Q still quits (ADR 0019).
  EXPECT_EQ(CommandForAccelerator(ui::Accelerator(
                ui::VKEY_Q, ui::EF_COMMAND_DOWN | ui::EF_CAPS_LOCK_ON)),
            Command::kQuit);
  EXPECT_EQ(CommandForAccelerator(ui::Accelerator(ui::VKEY_Q, ui::EF_NONE)),
            std::nullopt);
}

// The window registers the table's keys, but the platform's own: a field or
// the page takes its own Copy, and the app its own Hide.
TEST(CommandsTest, WindowKeysLeaveThePlatformsOwn) {
  const std::vector<ui::Accelerator> keys = WindowAccelerators();
  const std::set<ui::Accelerator> window(keys.begin(), keys.end());
  EXPECT_EQ(window.size(), keys.size());
  EXPECT_TRUE(
      window.contains(ui::Accelerator(ui::VKEY_T, ui::EF_COMMAND_DOWN)));
  EXPECT_TRUE(
      window.contains(ui::Accelerator(ui::VKEY_TAB, ui::EF_CONTROL_DOWN)));
  EXPECT_TRUE(
      window.contains(ui::Accelerator(ui::VKEY_Q, ui::EF_COMMAND_DOWN)));
  for (const ui::KeyboardCode key :
       {ui::VKEY_C, ui::VKEY_V, ui::VKEY_X, ui::VKEY_A, ui::VKEY_Z, ui::VKEY_H,
        ui::VKEY_M}) {
    EXPECT_FALSE(window.contains(ui::Accelerator(key, ui::EF_COMMAND_DOWN)));
  }
  // 32 of the menu bar's 41 keys: Hide, Hide Others, the Edit menu's six,
  // and Minimize left out.
  EXPECT_EQ(keys.size(), 32u);
}

// Ahead of the page, what Chrome reserves (ADR 0019): ⌘Q, ⌘T, ⌘W, ⇧⌘T,
// and the switcher's Ctrl-Tab; ⌘L, ⌘F, and ⌘1 go to the page first. While
// the page fills the screen, only Quit and Full Screen.
TEST(CommandsTest, AppFirstAsChromeReserves) {
  const auto app_first = [](bool page_fullscreen) {
    std::vector<Command> commands;
    for (const Menu& menu : MenuBarMenus()) {
      for (const MenuItem& item : menu.items) {
        if (item.command && IsAppFirst(*item.command, page_fullscreen)) {
          commands.push_back(*item.command);
        }
      }
    }
    return commands;
  };
  EXPECT_EQ(app_first(false),
            (std::vector<Command>{Command::kQuit, Command::kNewTab,
                                  Command::kCloseTab, Command::kReopenClosedTab,
                                  Command::kNextTab, Command::kPreviousTab}));
  EXPECT_EQ(app_first(true),
            (std::vector<Command>{Command::kQuit, Command::kFullScreen}));
}

TEST(CommandsTest, TabNumbers) {
  EXPECT_EQ(TabNumberOf(Command::kTab1), 1);
  EXPECT_EQ(TabNumberOf(Command::kTab9), 9);
  EXPECT_EQ(TabNumberOf(Command::kNextTab), 0);
  EXPECT_EQ(TabNumberOf(Command::kMinimize), 0);
}

// The menu bar's models hold the table: each menu a submenu, its items'
// titles and keys shown, ⌘='s item hidden; their states are the runner's,
// asked each time, and Enter Full Screen's title follows it.
TEST(CommandsTest, MenuBarModelsFromTheTable) {
  FakeRunner runner;
  MenuBar menu_bar(runner);
  ui::MenuModel& root = menu_bar.model();
  ASSERT_EQ(root.GetItemCount(), MenuBarMenus().size());
  EXPECT_EQ(root.GetTypeAt(0), ui::MenuModel::TYPE_SUBMENU);
  EXPECT_EQ(root.GetLabelAt(0), u"Pane");
  EXPECT_TRUE(root.IsEnabledAt(0));

  ui::MenuModel* view = root.GetSubmenuModelAt(3);
  ASSERT_EQ(root.GetLabelAt(3), u"View");
  ui::Accelerator key;
  std::vector<ui::Accelerator> hidden;
  size_t full_screen = 0;
  for (size_t index = 0; index < view->GetItemCount(); ++index) {
    if (view->GetTypeAt(index) == ui::MenuModel::TYPE_SEPARATOR) {
      continue;
    }
    EXPECT_TRUE(view->GetForceShowAcceleratorForItemAt(index));
    if (!view->IsVisibleAt(index)) {
      ASSERT_TRUE(view->GetAcceleratorAt(index, &key));
      hidden.push_back(key);
    }
    if (view->GetCommandIdAt(index) == static_cast<int>(Command::kFullScreen)) {
      full_screen = index;
    }
  }
  EXPECT_EQ(
      hidden,
      (std::vector<ui::Accelerator>{
          ui::Accelerator(ui::VKEY_OEM_PLUS, ui::EF_COMMAND_DOWN),
          ui::Accelerator(ui::VKEY_I, ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN)}));
  ASSERT_TRUE(view->IsItemDynamicAt(full_screen));
  EXPECT_EQ(view->GetLabelAt(full_screen), u"Enter Full Screen");
  runner.full_screen = true;
  EXPECT_EQ(view->GetLabelAt(full_screen), u"Exit Full Screen");

  ui::MenuModel* app = root.GetSubmenuModelAt(0);
  for (size_t index = 0; index < app->GetItemCount(); ++index) {
    if (app->GetCommandIdAt(index) == static_cast<int>(Command::kLock)) {
      EXPECT_FALSE(app->IsEnabledAt(index));
    }
    if (app->GetCommandIdAt(index) == static_cast<int>(Command::kQuit)) {
      EXPECT_TRUE(app->IsEnabledAt(index));
      app->ActivatedAt(index);
    }
  }
  EXPECT_EQ(runner.ran, std::vector<Command>{Command::kQuit});
}

}  // namespace pane
