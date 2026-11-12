// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_ANCHORED_MENU_H_
#define PANE_UI_DESIGN_ANCHORED_MENU_H_

#include <memory>

#include "base/functional/callback.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"

namespace gfx {
class Point;
class Rect;
}  // namespace gfx

namespace ui {
class MenuModel;
}

namespace views {
class Widget;
}

namespace pane {

// Where a menu opened from `trigger` (its bounds on the screen) shows its top
// left: just under the trigger, its leading edge on the trigger's, as a
// pop-up button's menu shows (the Swift's PaneMenuTrigger, Paper 05.7).
gfx::Point MenuAnchorUnder(const gfx::Rect& trigger);

// The system's menu of `model`, its top left at a point of the screen
// whatever opened it: a click, a key, or an assistive technology. On the Mac,
// views::MenuRunner shows a context menu where the current press or release
// is (MenuRunnerImplCocoa, through ui::EventForPositioningContextMenu), so a
// menu a click opens shows at the pointer, not at its anchor: there the
// platform's runs that runner's own pieces with the event moved to the anchor
// (anchored_menu_mac.mm). On Windows it is views::MenuRunner, whose menu shows
// at its anchor.
class AnchoredMenu {
 public:
  // The platform's. `model` outlives it. `gone` runs as the menu closes,
  // unless the menu goes first.
  static std::unique_ptr<AnchoredMenu> Make(ui::MenuModel& model,
                                            base::RepeatingClosure gone);

  AnchoredMenu(const AnchoredMenu&) = delete;
  AnchoredMenu& operator=(const AnchoredMenu&) = delete;
  // Deleted while its menu shows, even from inside the menu's own loop, it
  // closes the menu, and `gone` doesn't run.
  virtual ~AnchoredMenu() = default;

  // Shows the menu over `widget`, its top left at `anchor`, a point of the
  // screen. On the Mac it returns once the menu has closed, as the system's
  // menu runs in a loop of its own, the app's tasks held until it ends.
  virtual void Run(views::Widget& widget,
                   const gfx::Point& anchor,
                   ui::mojom::MenuSourceType source) = 0;
  virtual bool IsRunning() const = 0;

 protected:
  AnchoredMenu() = default;
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_ANCHORED_MENU_H_
