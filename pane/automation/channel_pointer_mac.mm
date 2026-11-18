// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/channel_pointer.h"

#import <AppKit/AppKit.h>

#include <memory>

#include "base/functional/bind.h"
#include "pane/ui/app/app_events.h"

namespace pane {

namespace {

// The pointer's events as the system hands them to the app, through its
// queue: the channel's moves and scrolls go to the window itself, past it,
// and its clicks are none of these (native_input_mac.mm).
constexpr NSEventMask kSystemPointer =
    NSEventMaskMouseMoved | NSEventMaskMouseEntered | NSEventMaskMouseExited |
    NSEventMaskLeftMouseDragged | NSEventMaskRightMouseDragged |
    NSEventMaskOtherMouseDragged | NSEventMaskScrollWheel;

// AppKit's monitor, removed with it.
class Monitor {
 public:
  explicit Monitor(id monitor) : monitor_(monitor) {}
  Monitor(const Monitor&) = delete;
  Monitor& operator=(const Monitor&) = delete;
  ~Monitor() { [NSEvent removeMonitor:monitor_]; }

 private:
  id __strong monitor_;
};

}  // namespace

// static
base::ScopedClosureRunner ChannelPointer::KeepSystemPointerOut() {
  // In front the pointer is a person's, as in a pass by hand (qa-hold).
  if (MayTakeFront()) {
    return base::ScopedClosureRunner();
  }
  id monitor =
      [NSEvent addLocalMonitorForEventsMatchingMask:kSystemPointer
                                            handler:^NSEvent*(NSEvent* event) {
                                              return nil;
                                            }];
  return base::ScopedClosureRunner(base::BindOnce(
      [](std::unique_ptr<Monitor>) {}, std::make_unique<Monitor>(monitor)));
}

}  // namespace pane
