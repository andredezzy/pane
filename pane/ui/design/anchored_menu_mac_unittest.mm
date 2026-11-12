// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/anchored_menu_mac.h"

#import <AppKit/AppKit.h>

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/cocoa/menu_utils.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view.h"

namespace pane {

namespace {

// Two options, counting how often their menu shows and closes, and which was
// chosen.
class Options : public ui::SimpleMenuModel,
                public ui::SimpleMenuModel::Delegate {
 public:
  Options() : ui::SimpleMenuModel(this) {
    AddCheckItem(0, u"Automatic");
    AddCheckItem(1, u"UTC");
  }

  // ui::SimpleMenuModel:
  void MenuWillShow() override {
    ++shows;
    ui::SimpleMenuModel::MenuWillShow();
  }
  void MenuWillClose() override {
    ++closes;
    ui::SimpleMenuModel::MenuWillClose();
  }

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override {
    return command_id == 0;
  }
  void ExecuteCommand(int command_id, int event_flags) override {}

  int shows = 0;
  int closes = 0;
};

// What a press on a trigger looks like as it opens the menu: AppKit's
// current event, at the pointer.
NSEvent* PressAt(NSPoint point) {
  return [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                            location:point
                       modifierFlags:NSEventModifierFlagOption
                           timestamp:1234.5
                        windowNumber:7
                             context:nil
                         eventNumber:42
                          clickCount:1
                            pressure:1];
}

// A widget's root view, which keeps the view a press went to as that press's
// handler until its release, as Views' RootView does.
class PressedRoot : public views::View {
 public:
  // views::View:
  void SetMouseAndGestureHandler(views::View* new_handler) override {
    handler = new_handler;
  }

  raw_ptr<views::View> handler = nullptr;
};

class AnchoredMenuMacTest : public testing::Test {
 protected:
  // SimpleMenuModel tells its delegate of a close in a task.
  base::test::SingleThreadTaskEnvironment task_environment_;
};

}  // namespace

// The press that opened the menu, moved to the anchor and the same press in
// every other way: the system shows the menu's top left there, not at the
// pointer, and still takes the press's release as the end of a click.
TEST_F(AnchoredMenuMacTest, APressShowsTheMenuAtTheAnchorAndKeepsItsRelease) {
  NSEvent* moved = MenuEventAt(PressAt(NSMakePoint(640, 412)),
                               NSMakePoint(120, 300), /*window_number=*/7);

  EXPECT_TRUE(NSEqualPoints(moved.locationInWindow, NSMakePoint(120, 300)));
  EXPECT_EQ(moved.type, NSEventTypeLeftMouseDown);
  EXPECT_EQ(moved.timestamp, 1234.5);
  EXPECT_EQ(moved.eventNumber, 42);
  EXPECT_EQ(moved.clickCount, 1);
  EXPECT_EQ(moved.pressure, 1);
  EXPECT_EQ(moved.windowNumber, 7);
  EXPECT_EQ(moved.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask,
            NSEventModifierFlagOption);
}

// With no press under way (a key, or an assistive technology, opened it),
// Chromium's own event is already at the anchor, and stays as it is.
TEST_F(AnchoredMenuMacTest, AKeyShowsTheMenuAtTheAnchorAsChromiumDoes) {
  NSEvent* chromiums = ui::EventForPositioningContextMenuRelativeToWindow(
      NSMakePoint(120, 300), nil);
  ASSERT_EQ(chromiums.type, NSEventTypeRightMouseDown);

  NSEvent* moved = MenuEventAt(chromiums, NSMakePoint(120, 300),
                               /*window_number=*/0);

  EXPECT_TRUE(NSEqualPoints(moved.locationInWindow, NSMakePoint(120, 300)));
  EXPECT_EQ(moved.type, NSEventTypeRightMouseDown);
  EXPECT_EQ(moved.timestamp, chromiums.timestamp);
}

// The menu runs while the system's loop does, the model told it shows and
// closes, and `gone` runs once it has closed: what lights the trigger and
// puts it out, and what keeps a second press from opening another.
TEST_F(AnchoredMenuMacTest, ClosingTellsTheTriggerOnce) {
  Options model;
  int gone = 0;
  std::unique_ptr<AnchoredMenuMac> menu;
  menu = std::make_unique<AnchoredMenuMac>(
      model, base::BindLambdaForTesting([&] { ++gone; }),
      base::BindLambdaForTesting(
          [&](NSMenu* shown, NSEvent* event, NSView*, ui::ElementContext) {
            EXPECT_TRUE(menu->IsRunning());
            EXPECT_EQ(shown.numberOfItems, 2);
            EXPECT_TRUE(
                NSEqualPoints(event.locationInWindow, NSMakePoint(120, 300)));
            // As AppKit tells the menu's delegate.
            [shown.delegate menuWillOpen:shown];
            EXPECT_EQ(model.shows, 1);
            EXPECT_EQ(gone, 0);
            [shown.delegate menuDidClose:shown];
          }));

  menu->RunAt(nullptr, nil, PressAt(NSMakePoint(120, 300)), nil,
              ui::ElementContext());

  EXPECT_EQ(gone, 1);
  EXPECT_EQ(model.closes, 1);
  EXPECT_FALSE(menu->IsRunning());
}

// The menu takes the release of the press that opened it, which never
// reaches the window: the root view forgets the trigger as that press's
// handler before the menu shows, as views::MenuRunner has it forget, or it
// sends the trigger the next press wherever it lands, and the menu opens
// again under the trigger (Settings' menus each opened Theme's).
TEST_F(AnchoredMenuMacTest, TheRootViewForgetsThePressThatOpenedTheMenu) {
  Options model;
  views::View trigger;
  PressedRoot root;
  root.handler = &trigger;
  bool shown_without_handler = false;
  AnchoredMenuMac menu(
      model, base::RepeatingClosure(),
      base::BindLambdaForTesting(
          [&](NSMenu* shown, NSEvent*, NSView*, ui::ElementContext) {
            shown_without_handler = root.handler == nullptr;
            [shown.delegate menuWillOpen:shown];
            [shown.delegate menuDidClose:shown];
          }));

  menu.RunAt(&root, nil, PressAt(NSMakePoint(120, 300)), nil,
             ui::ElementContext());

  EXPECT_TRUE(shown_without_handler);
  EXPECT_EQ(root.handler, nullptr);
}

// A trigger deleted under its open menu (the sheet closing as the Mac locks):
// its menu goes first, closing the menu and its controller while the models
// stand, then the models, as MenuButton's members go; the system's loop ends
// after, with the menu it still holds, and nothing is told of it.
TEST_F(AnchoredMenuMacTest, ATriggerDeletedUnderItsMenuClosesItSafely) {
  auto model = std::make_unique<Options>();
  int gone = 0;
  std::unique_ptr<AnchoredMenuMac> menu;
  menu = std::make_unique<AnchoredMenuMac>(
      *model, base::BindLambdaForTesting([&] { ++gone; }),
      base::BindLambdaForTesting(
          [&](NSMenu* shown, NSEvent*, NSView*, ui::ElementContext) {
            [shown.delegate menuWillOpen:shown];

            menu.reset();
            EXPECT_EQ(model->closes, 1);
            EXPECT_TRUE(shown.delegate == nil);
            model.reset();

            [shown.delegate menuDidClose:shown];
          }));

  AnchoredMenuMac* running = menu.get();
  running->RunAt(nullptr, nil, PressAt(NSMakePoint(120, 300)), nil,
                 ui::ElementContext());

  EXPECT_FALSE(menu);
  EXPECT_EQ(gone, 0);
}

}  // namespace pane
