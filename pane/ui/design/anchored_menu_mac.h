// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_ANCHORED_MENU_MAC_H_
#define PANE_UI_DESIGN_ANCHORED_MENU_MAC_H_

#import <AppKit/AppKit.h>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "pane/ui/design/anchored_menu.h"
#include "ui/base/interaction/element_identifier.h"

@class MenuControllerCocoa;
@class MenuControllerCocoaDelegateImpl;

namespace pane {

// `event` moved to `location`, a point of the window `window_number` names,
// as itself in every other way: the system shows a context menu's top left
// at its event, and a menu a press opens takes the press's release, which
// its kind, time, and number tell.
NSEvent* MenuEventAt(NSEvent* event, NSPoint location, NSInteger window_number);

// The Mac's AnchoredMenu: Chromium's own runner of a context menu
// (MenuRunnerImplCocoa) in its pieces, the model's NSMenu made by
// MenuControllerCocoa with Views' delegate and shown by ui::ShowContextMenu,
// its tasks held as that runner holds them, with the event Chromium places it
// by moved to the anchor.
class AnchoredMenuMac : public AnchoredMenu {
 public:
  // What shows `menu` over `view` at `event`, returning once it has closed:
  // ui::ShowContextMenu, or a test's in place of the system's loop.
  using Show = base::RepeatingCallback<
      void(NSMenu* menu, NSEvent* event, NSView* view, ui::ElementContext)>;

  // With no `show`, the system's.
  AnchoredMenuMac(ui::MenuModel& model,
                  base::RepeatingClosure gone,
                  Show show = Show());
  ~AnchoredMenuMac() override;

  // AnchoredMenu:
  void Run(views::Widget& widget,
           const gfx::Point& anchor,
           ui::mojom::MenuSourceType source) override;
  bool IsRunning() const override;

  // What Run does once the widget has given it these: the menu shown over
  // `view` at `event`, its items given `delegate`'s look.
  void RunAt(NSView* view,
             NSEvent* event,
             MenuControllerCocoaDelegateImpl* delegate,
             ui::ElementContext context);

 private:
  raw_ptr<ui::MenuModel> model_;
  base::RepeatingClosure gone_;
  Show show_;
  MenuControllerCocoa* __strong controller_;
  MenuControllerCocoaDelegateImpl* __strong delegate_;
  bool running_ = false;
  base::WeakPtrFactory<AnchoredMenuMac> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_ANCHORED_MENU_MAC_H_
