// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/commands.h"

#include <optional>
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
std::string MacKeyOf(const ui::Accelerator& accelerator) {
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

// A key as Windows' menus write it: Ctrl+Alt+Shift+, then the key.
std::string WindowsKeyOf(const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_UNKNOWN) {
    return std::string();
  }
  EXPECT_FALSE(accelerator.IsCmdDown()) << "Windows has no ⌘";
  std::string key;
  if (accelerator.IsCtrlDown()) {
    key += "Ctrl+";
  }
  if (accelerator.IsAltDown()) {
    key += "Alt+";
  }
  // Shift+= is the keyboard's +.
  const bool plus =
      accelerator.key_code() == ui::VKEY_OEM_PLUS && accelerator.IsShiftDown();
  if (accelerator.IsShiftDown() && !plus) {
    key += "Shift+";
  }
  switch (accelerator.key_code()) {
    case ui::VKEY_OEM_COMMA:
      return key + ",";
    case ui::VKEY_OEM_PLUS:
      return key + (plus ? "+" : "=");
    case ui::VKEY_OEM_MINUS:
      return key + "-";
    case ui::VKEY_TAB:
      return key + "Tab";
    case ui::VKEY_LEFT:
      return key + "Left";
    case ui::VKEY_RIGHT:
      return key + "Right";
    case ui::VKEY_F4:
      return key + "F4";
    case ui::VKEY_F11:
      return key + "F11";
    case ui::VKEY_F12:
      return key + "F12";
    case ui::VKEY_ADD:
      return key + "Num+";
    case ui::VKEY_SUBTRACT:
      return key + "Num-";
    default:
      return key + static_cast<char>(accelerator.key_code());
  }
}

// Each menu's items as "Title Key", separators as "-", hidden ones marked.
std::vector<std::string> Lines(const Menu& menu, KeySystem system) {
  std::vector<std::string> lines;
  for (const MenuItem& item : menu.items) {
    if (!item.command) {
      lines.push_back("-");
      continue;
    }
    std::string line = base::UTF16ToUTF8(item.title);
    const std::string key = system == KeySystem::kMac
                                ? MacKeyOf(item.accelerator)
                                : WindowsKeyOf(item.accelerator);
    if (!key.empty()) {
      line += " " + key;
    }
    if (item.hidden) {
      line += " (hidden)";
    }
    lines.push_back(line);
  }
  return lines;
}

// This system's ⌘ or Ctrl.
int Primary() {
  return ThisKeySystem() == KeySystem::kMac ? ui::EF_COMMAND_DOWN
                                            : ui::EF_CONTROL_DOWN;
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

namespace {

using Listing = std::vector<std::pair<std::string, std::vector<std::string>>>;

void ExpectMenus(KeySystem system, const Listing& expected) {
  const std::vector<Menu> menus = MenusWithKeysOf(system);
  ASSERT_EQ(menus.size(), expected.size());
  for (size_t index = 0; index < menus.size(); ++index) {
    EXPECT_EQ(base::UTF16ToUTF8(menus[index].title), expected[index].first);
    EXPECT_EQ(Lines(menus[index], system), expected[index].second)
        << expected[index].first;
  }
}

}  // namespace

// The Swift's MainMenu, item for item: every menu, title, key, and
// separator, ⌘= Zoom In hidden under ⌘+'s.
TEST(CommandsTest, MenusAreTheSwifts) {
  ExpectMenus(
      KeySystem::kMac,
      {
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
            "Reload ⌘R", "-", "Actual Size ⌘0", "Zoom In ⌘+",
            "Zoom In ⌘= (hidden)", "Zoom Out ⌘-", "-", "Enter Full Screen ⌃⌘F",
            "-", "Developer Tools ⇧⌘I", "Developer Tools ⌥⌘I (hidden)"}},
          {"Navigate", {"Back ⌘[", "Forward ⌘]", "-", "Focus Address Bar ⌘L"}},
          {"Tab",
           {"Next Tab (Recent) ⌃⇥", "Previous Tab (Recent) ⌃⇧⇥", "-",
            "Tab 1 ⌘1", "Tab 2 ⌘2", "Tab 3 ⌘3", "Tab 4 ⌘4", "Tab 5 ⌘5",
            "Tab 6 ⌘6", "Tab 7 ⌘7", "Tab 8 ⌘8", "Last Tab ⌘9"}},
          {"Window", {"Minimize ⌘M", "Zoom", "-", "Extensions"}},
      });
}

// The same table on Windows (ADR 0019): Ctrl where the Mac takes ⌘, and
// Chrome's own Windows keys where Ctrl alone would collide (⌃⌘F, ⌃⌘L) or
// Windows has its own: F11, Alt+Left and Alt+Right, F12, Ctrl+Y, Ctrl+= with
// Ctrl++ and the numpad's keys, and Alt+F4 beside Ctrl+Q (ADR 0053). The
// Mac's alone has no key there.
TEST(CommandsTest, WindowsTakesCtrlAndItsOwnKeys) {
  ExpectMenus(
      KeySystem::kWindows,
      {
          {"Pane",
           {"About Pane", "-", "Check for Updates…", "-", "Settings… Ctrl+,",
            "-", "Lock Pane Ctrl+Shift+L", "-", "Hide Pane", "Hide Others",
            "Show All", "-", "Quit Pane Ctrl+Q", "Quit Pane Alt+F4 (hidden)"}},
          {"File",
           {"New Tab Ctrl+T", "Close Tab Ctrl+W",
            "Reopen Closed Tab Ctrl+Shift+T", "-", "Print… Ctrl+P"}},
          {"Edit",
           {"Undo Ctrl+Z", "Redo Ctrl+Y", "-", "Cut Ctrl+X", "Copy Ctrl+C",
            "Paste Ctrl+V", "Select All Ctrl+A"}},
          {"View",
           {"Find in Page Ctrl+F", "Find Next Ctrl+G",
            "Find Previous Ctrl+Shift+G", "-", "Reload Ctrl+R", "-",
            "Actual Size Ctrl+0", "Zoom In Ctrl+=", "Zoom In Ctrl++ (hidden)",
            "Zoom In Ctrl+Num+ (hidden)", "Zoom Out Ctrl+-",
            "Zoom Out Ctrl+Shift+- (hidden)", "Zoom Out Ctrl+Num- (hidden)",
            "-", "Enter Full Screen F11", "-", "Developer Tools Ctrl+Shift+I",
            "Developer Tools F12 (hidden)"}},
          {"Navigate",
           {"Back Alt+Left", "Forward Alt+Right", "-",
            "Focus Address Bar Ctrl+L"}},
          {"Tab",
           {"Next Tab (Recent) Ctrl+Tab",
            "Previous Tab (Recent) Ctrl+Shift+Tab", "-", "Tab 1 Ctrl+1",
            "Tab 2 Ctrl+2", "Tab 3 Ctrl+3", "Tab 4 Ctrl+4", "Tab 5 Ctrl+5",
            "Tab 6 Ctrl+6", "Tab 7 Ctrl+7", "Tab 8 Ctrl+8", "Last Tab Ctrl+9"}},
          {"Window", {"Minimize", "Zoom", "-", "Extensions"}},
      });
}

// Zoom In takes what people type on Windows, Ctrl+=, with Ctrl++ (Ctrl+Shift+=)
// and the numpad's + beside it, and Zoom Out Ctrl+-, Ctrl+Shift+-, and the
// numpad's -, as Chrome's Windows table has them; the Mac keeps ⌘+ shown and
// ⌘= beside it.
TEST(CommandsTest, ZoomKeys) {
  const auto commands_of = [](KeySystem system, const ui::Accelerator& key) {
    std::vector<Command> commands;
    for (const Menu& menu : MenusWithKeysOf(system)) {
      for (const MenuItem& item : menu.items) {
        if (item.command && item.accelerator == key) {
          commands.push_back(*item.command);
        }
      }
    }
    return commands;
  };
  const std::vector<Command> zoom_in{Command::kZoomIn};
  const std::vector<Command> zoom_out{Command::kZoomOut};
  constexpr int kCtrl = ui::EF_CONTROL_DOWN;
  constexpr int kShift = ui::EF_SHIFT_DOWN;
  for (const ui::Accelerator& key :
       {ui::Accelerator(ui::VKEY_OEM_PLUS, kCtrl),
        ui::Accelerator(ui::VKEY_OEM_PLUS, kCtrl | kShift),
        ui::Accelerator(ui::VKEY_ADD, kCtrl)}) {
    EXPECT_EQ(commands_of(KeySystem::kWindows, key), zoom_in);
  }
  for (const ui::Accelerator& key :
       {ui::Accelerator(ui::VKEY_OEM_MINUS, kCtrl),
        ui::Accelerator(ui::VKEY_OEM_MINUS, kCtrl | kShift),
        ui::Accelerator(ui::VKEY_SUBTRACT, kCtrl)}) {
    EXPECT_EQ(commands_of(KeySystem::kWindows, key), zoom_out);
  }
  EXPECT_EQ(commands_of(KeySystem::kMac,
                        ui::Accelerator(ui::VKEY_OEM_PLUS,
                                        ui::EF_COMMAND_DOWN | kShift)),
            zoom_in);
  EXPECT_EQ(commands_of(KeySystem::kMac, ui::Accelerator(ui::VKEY_OEM_PLUS,
                                                         ui::EF_COMMAND_DOWN)),
            zoom_in);
}

// A key runs one command, on each system: no two items hold the same one.
TEST(CommandsTest, EachKeyRunsOneCommand) {
  for (const KeySystem system : {KeySystem::kMac, KeySystem::kWindows}) {
    std::set<ui::Accelerator> seen;
    for (const Menu& menu : MenusWithKeysOf(system)) {
      for (const MenuItem& item : menu.items) {
        if (item.command && item.accelerator.key_code() != ui::VKEY_UNKNOWN) {
          EXPECT_TRUE(seen.insert(item.accelerator).second)
              << base::UTF16ToUTF8(item.title);
        }
      }
    }
  }
  for (const Menu& menu : MenuBarMenus()) {
    for (const MenuItem& item : menu.items) {
      if (item.command && item.accelerator.key_code() != ui::VKEY_UNKNOWN) {
        EXPECT_EQ(CommandForAccelerator(item.accelerator), item.command);
      }
    }
  }
  // Caps Lock changes nothing: ⌘Q, Ctrl+Q on Windows, still quits (ADR
  // 0019).
  EXPECT_EQ(CommandForAccelerator(
                ui::Accelerator(ui::VKEY_Q, Primary() | ui::EF_CAPS_LOCK_ON)),
            Command::kQuit);
  EXPECT_EQ(CommandForAccelerator(ui::Accelerator(ui::VKEY_Q, ui::EF_NONE)),
            std::nullopt);
}

// The key a command's item shows: Exit Full Screen's in Chrome's own menus.
TEST(CommandsTest, ShownKeys) {
  EXPECT_EQ(ShownAcceleratorOf(Command::kQuit),
            ui::Accelerator(ui::VKEY_Q, Primary()));
  // ⌘+ on the Mac, Ctrl+= on Windows: what each shows, as people type it.
  EXPECT_EQ(ShownAcceleratorOf(Command::kZoomIn),
            ThisKeySystem() == KeySystem::kMac
                ? ui::Accelerator(ui::VKEY_OEM_PLUS,
                                  ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN)
                : ui::Accelerator(ui::VKEY_OEM_PLUS, ui::EF_CONTROL_DOWN));
  EXPECT_EQ(ShownAcceleratorOf(Command::kAbout).key_code(), ui::VKEY_UNKNOWN);
}

// The window registers the table's keys, but the platform's own: a field or
// the page takes its own Copy, and the app its own Hide.
TEST(CommandsTest, WindowKeysLeaveThePlatformsOwn) {
  const std::vector<ui::Accelerator> keys = WindowAccelerators();
  const std::set<ui::Accelerator> window(keys.begin(), keys.end());
  EXPECT_EQ(window.size(), keys.size());
  EXPECT_TRUE(window.contains(ui::Accelerator(ui::VKEY_T, Primary())));
  EXPECT_TRUE(
      window.contains(ui::Accelerator(ui::VKEY_TAB, ui::EF_CONTROL_DOWN)));
  EXPECT_TRUE(window.contains(ui::Accelerator(ui::VKEY_Q, Primary())));
  for (const ui::KeyboardCode key :
       {ui::VKEY_C, ui::VKEY_V, ui::VKEY_X, ui::VKEY_A, ui::VKEY_Z, ui::VKEY_H,
        ui::VKEY_M}) {
    EXPECT_FALSE(window.contains(ui::Accelerator(key, Primary())));
  }
  // On the Mac, 32 of the menu bar's 41 keys: Hide, Hide Others, the Edit
  // menu's six, and Minimize left out. On Windows, the same 32, Alt+F4, and
  // the zooms' Ctrl+Shift+- and numpad keys, of 42: the Edit menu's six left
  // out.
  EXPECT_EQ(keys.size(), ThisKeySystem() == KeySystem::kMac ? 32u : 36u);
}

// Ahead of the page, what Chrome reserves (ADR 0019): ⌘Q, ⌘T, ⌘W, ⇧⌘T,
// and the switcher's Ctrl-Tab, Ctrl where Windows has it; ⌘L, ⌘F, and ⌘1 go
// to the page first. While the page fills the screen, only Quit and Full
// Screen.
TEST(CommandsTest, AppFirstAsChromeReserves) {
  const auto app_first = [](bool page_fullscreen) {
    std::vector<Command> commands;
    for (const Menu& menu : MenuBarMenus()) {
      for (const MenuItem& item : menu.items) {
        // A hidden item repeats a shown one's command (Windows' Alt+F4).
        if (item.command && !item.hidden &&
            IsAppFirst(*item.command, page_fullscreen)) {
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

// Ported from PaneKit's KeyRoutingTests (newTab): ⌘T is New Tab, ahead of the
// page, Caps Lock or not; ⇧⌘T is another command, and a typed t none.
TEST(CommandsTest, NewTabKey) {
  for (const int flags : {Primary(), Primary() | ui::EF_CAPS_LOCK_ON}) {
    const std::optional<Command> command =
        CommandForAccelerator(ui::Accelerator(ui::VKEY_T, flags));
    ASSERT_EQ(command, Command::kNewTab);
    EXPECT_TRUE(IsAppFirst(*command, /*page_fullscreen=*/false));
  }
  EXPECT_EQ(CommandForAccelerator(
                ui::Accelerator(ui::VKEY_T, Primary() | ui::EF_SHIFT_DOWN)),
            Command::kReopenClosedTab);
  EXPECT_EQ(CommandForAccelerator(ui::Accelerator(ui::VKEY_T, ui::EF_NONE)),
            std::nullopt);
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
  EXPECT_EQ(hidden,
            ThisKeySystem() == KeySystem::kMac
                ? (std::vector<ui::Accelerator>{
                      ui::Accelerator(ui::VKEY_OEM_PLUS, ui::EF_COMMAND_DOWN),
                      ui::Accelerator(ui::VKEY_I,
                                      ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN)})
                : (std::vector<ui::Accelerator>{
                      ui::Accelerator(ui::VKEY_OEM_PLUS,
                                      ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN),
                      ui::Accelerator(ui::VKEY_ADD, ui::EF_CONTROL_DOWN),
                      ui::Accelerator(ui::VKEY_OEM_MINUS,
                                      ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN),
                      ui::Accelerator(ui::VKEY_SUBTRACT, ui::EF_CONTROL_DOWN),
                      ui::Accelerator(ui::VKEY_F12, ui::EF_NONE)}));
  ASSERT_TRUE(view->IsItemDynamicAt(full_screen));
  EXPECT_EQ(view->GetLabelAt(full_screen), u"Enter Full Screen");
  runner.full_screen = true;
  EXPECT_EQ(view->GetLabelAt(full_screen), u"Exit Full Screen");

  ui::MenuModel* app = root.GetSubmenuModelAt(0);
  for (size_t index = 0; index < app->GetItemCount(); ++index) {
    if (app->GetCommandIdAt(index) == static_cast<int>(Command::kLock)) {
      EXPECT_FALSE(app->IsEnabledAt(index));
    }
    // The shown Quit, not Windows' hidden Alt+F4 beside it.
    if (app->GetCommandIdAt(index) == static_cast<int>(Command::kQuit) &&
        app->IsVisibleAt(index)) {
      EXPECT_TRUE(app->IsEnabledAt(index));
      app->ActivatedAt(index);
    }
  }
  EXPECT_EQ(runner.ran, std::vector<Command>{Command::kQuit});
}

}  // namespace pane
