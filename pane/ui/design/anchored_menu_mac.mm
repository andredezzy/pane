// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/anchored_menu_mac.h"

#include <utility>

#include "base/check.h"
#import "components/remote_cocoa/app_shim/menu_controller_cocoa_delegate_impl.h"
#include "ui/base/cocoa/menu_utils.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/mac/coordinate_conversion.h"
#import "ui/menus/cocoa/menu_controller.h"
#include "ui/views/controls/menu/menu_controller_cocoa_delegate_params.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace pane {

NSEvent* MenuEventAt(NSEvent* event,
                     NSPoint location,
                     NSInteger window_number) {
  return [NSEvent mouseEventWithType:event.type
                            location:location
                       modifierFlags:event.modifierFlags
                           timestamp:event.timestamp
                        windowNumber:window_number
                             context:nil
                         eventNumber:event.eventNumber
                          clickCount:event.clickCount
                            pressure:event.pressure];
}

// static
std::unique_ptr<AnchoredMenu> AnchoredMenu::Make(ui::MenuModel& model,
                                                 base::RepeatingClosure gone) {
  return std::make_unique<AnchoredMenuMac>(model, std::move(gone));
}

AnchoredMenuMac::AnchoredMenuMac(ui::MenuModel& model,
                                 base::RepeatingClosure gone,
                                 Show show)
    : model_(&model), gone_(std::move(gone)), show_(std::move(show)) {}

AnchoredMenuMac::~AnchoredMenuMac() {
  // As MenuRunnerImplCocoa::Release: the menu closed, and its controller gone
  // while the model stands, as menu_controller.h asks; the menu's loop ends
  // after this, and RunAt then touches nothing of it.
  [controller_ cancel];
  controller_ = nil;
}

void AnchoredMenuMac::Run(views::Widget& widget,
                          const gfx::Point& anchor,
                          ui::mojom::MenuSourceType /*source*/) {
  NSWindow* window = widget.GetNativeWindow().GetNativeNSWindow();
  const NSPoint location =
      [window convertPointFromScreen:gfx::ScreenPointToNSPoint(anchor)];
  // The event Chromium's runner places the menu by, the press or release
  // under way or one it makes at the anchor, moved to the anchor.
  RunAt(widget.GetRootView(), widget.GetNativeView().GetNativeNSView(),
        MenuEventAt(ui::EventForPositioningContextMenuRelativeToWindow(location,
                                                                       window),
                    location, window.windowNumber),
        [[MenuControllerCocoaDelegateImpl alloc]
            initWithParams:views::MenuControllerParamsForWidget(&widget)],
        views::ElementTrackerViews::GetContextForWidget(&widget));
}

bool AnchoredMenuMac::IsRunning() const {
  return running_;
}

void AnchoredMenuMac::RunAt(views::View* root,
                            NSView* view,
                            NSEvent* event,
                            MenuControllerCocoaDelegateImpl* delegate,
                            ui::ElementContext context) {
  CHECK(!running_);
  // As views::MenuRunner::RunMenuAt: the menu takes the release of the press
  // that opened it, so the root view would keep the trigger as that press's
  // handler and send it the next press wherever it lands, opening this menu
  // again under it.
  if (root) {
    root->SetMouseAndGestureHandler(nullptr);
  }
  delegate_ = delegate;
  controller_ = [[MenuControllerCocoa alloc] initWithModel:model_
                                             isContextMenu:YES
                                                  delegate:delegate_];
  running_ = true;
  const base::WeakPtr<AnchoredMenuMac> alive = weak_factory_.GetWeakPtr();
  // A copy, which outlives this if the loop deletes it.
  if (const Show show = show_) {
    show.Run(controller_.menu, event, view, context);
  } else {
    ui::ShowContextMenu(controller_.menu, event, view,
                        /*allow_nested_tasks=*/false, context);
  }
  // Deleted under its menu, which its going closed.
  if (!alive) {
    return;
  }
  running_ = false;
  if (gone_) {
    gone_.Run();
  }
}

}  // namespace pane
