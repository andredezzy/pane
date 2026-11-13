// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/menu_bar.h"

#import <AppKit/AppKit.h>

#include <utility>

#include "base/strings/sys_string_conversions.h"
#include "ui/base/models/menu_model.h"
#import "ui/menus/cocoa/menu_controller.h"

namespace {

// AppKit's action for a platform command, which goes up the responder chain
// from what has the keyboard (the field's or the page's Cut, the window's
// Minimize, the app's Hide), as the Swift's items did; nil for the app's own.
SEL PlatformAction(pane::Command command) {
  switch (command) {
    case pane::Command::kAbout:
      return @selector(orderFrontStandardAboutPanel:);
    case pane::Command::kHide:
      return @selector(hide:);
    case pane::Command::kHideOthers:
      return @selector(hideOtherApplications:);
    case pane::Command::kShowAll:
      return @selector(unhideAllApplications:);
    case pane::Command::kUndo:
      return @selector(undo:);
    case pane::Command::kRedo:
      return @selector(redo:);
    case pane::Command::kCut:
      return @selector(cut:);
    case pane::Command::kCopy:
      return @selector(copy:);
    case pane::Command::kPaste:
      return @selector(paste:);
    case pane::Command::kSelectAll:
      return @selector(selectAll:);
    case pane::Command::kMinimize:
      return @selector(performMiniaturize:);
    case pane::Command::kZoom:
      return @selector(performZoom:);
    default:
      return nullptr;
  }
}

// A key equivalent as the menu shows it: ⌃⌥⇧⌘, then the key.
NSString* KeyOf(NSMenuItem* item) {
  if (!item.keyEquivalent.length) {
    return @"";
  }
  NSString* equivalent = item.keyEquivalent;
  NSEventModifierFlags flags = item.keyEquivalentModifierMask;
  // An uppercase key is Shift's, as AppKit reads it (ADR 0019).
  if (![equivalent isEqualToString:equivalent.lowercaseString]) {
    flags |= NSEventModifierFlagShift;
  }
  NSMutableString* key = [NSMutableString string];
  if (flags & NSEventModifierFlagControl) {
    [key appendString:@"⌃"];
  }
  if (flags & NSEventModifierFlagOption) {
    [key appendString:@"⌥"];
  }
  if (flags & NSEventModifierFlagShift) {
    [key appendString:@"⇧"];
  }
  if (flags & NSEventModifierFlagCommand) {
    [key appendString:@"⌘"];
  }
  if ([equivalent isEqualToString:@"\t"]) {
    equivalent = @"⇥";
  }
  [key appendString:equivalent.uppercaseString];
  return key;
}

base::ListValue DescribeMenu(NSMenu* menu) {
  base::ListValue items;
  for (NSMenuItem* item in menu.itemArray) {
    if (item.separatorItem) {
      continue;
    }
    base::DictValue entry =
        base::DictValue()
            .Set("title", base::SysNSStringToUTF8(item.title))
            .Set("key", base::SysNSStringToUTF8(KeyOf(item)))
            .Set("enabled", static_cast<bool>(item.enabled))
            .Set("hidden", static_cast<bool>(item.hidden))
            .Set("checked", item.state == NSControlStateValueOn);
    if (item.submenu) {
      [item.submenu update];
      entry.Set("submenu", DescribeMenu(item.submenu));
    }
    items.Append(std::move(entry));
  }
  return items;
}

}  // namespace

// Each item as MenuControllerCocoa makes it: a menu takes its title, which
// the menu bar shows; a platform command takes AppKit's action, which the
// responder chain enables and runs; a hidden item keeps its key.
@interface MenuBarItemsForPane : NSObject <MenuControllerCocoaDelegate>
@end

@implementation MenuBarItemsForPane

- (void)controllerWillAddItem:(NSMenuItem*)menuItem
                    fromModel:(ui::MenuModel*)model
                      atIndex:(size_t)index {
  if (menuItem.submenu) {
    menuItem.submenu.title = menuItem.title;
    // AppKit lists the windows in it, as the Swift's did.
    if ([menuItem.title isEqualToString:@"Window"]) {
      NSApp.windowsMenu = menuItem.submenu;
    }
    return;
  }
  if (SEL action = PlatformAction(
          static_cast<pane::Command>(model->GetCommandIdAt(index)))) {
    menuItem.target = nil;
    menuItem.action = action;
  }
  if (!model->IsVisibleAt(index)) {
    menuItem.hidden = YES;
    menuItem.allowsKeyEquivalentWhenHidden = YES;
  }
}

- (void)controllerWillAddMenu:(NSMenu*)menu fromModel:(ui::MenuModel*)model {
}

@end

namespace pane {

namespace {

class NativeMenuBarMac : public MenuBar::Native {
 public:
  explicit NativeMenuBarMac(ui::MenuModel& model) {
    items_ = [[MenuBarItemsForPane alloc] init];
    controller_ = [[MenuControllerCocoa alloc] initWithModel:&model
                                               isContextMenu:NO
                                                    delegate:items_];
    NSApp.mainMenu = controller_.menu;
  }

  ~NativeMenuBarMac() override {
    if (NSApp.mainMenu == controller_.menu) {
      NSApp.windowsMenu = nil;
      NSApp.mainMenu = nil;
    }
  }

  std::optional<bool> Choose(const std::vector<std::u16string>& path) override {
    NSMenu* menu = NSApp.mainMenu;
    if (path.empty() || !menu) {
      return std::nullopt;
    }
    for (size_t step = 0; step + 1 < path.size(); ++step) {
      NSMenuItem* item =
          [menu itemWithTitle:base::SysUTF16ToNSString(path[step])];
      if (!item.submenu) {
        return std::nullopt;
      }
      menu = item.submenu;
    }
    // As the menu opens: enabled states and titles brought up to date.
    [menu update];
    const NSInteger index =
        [menu indexOfItemWithTitle:base::SysUTF16ToNSString(path.back())];
    if (index < 0) {
      return std::nullopt;
    }
    if (![menu itemAtIndex:index].enabled) {
      return false;
    }
    [menu performActionForItemAtIndex:index];
    return true;
  }

  base::ListValue Describe() override {
    NSMenu* menu = NSApp.mainMenu;
    if (!menu) {
      return base::ListValue();
    }
    [menu update];
    return DescribeMenu(menu);
  }

 private:
  MenuBarItemsForPane* __strong items_;
  MenuControllerCocoa* __strong controller_;
};

}  // namespace

// static
std::unique_ptr<MenuBar::Native> MenuBar::Native::Show(ui::MenuModel& model) {
  return std::make_unique<NativeMenuBarMac>(model);
}

}  // namespace pane
