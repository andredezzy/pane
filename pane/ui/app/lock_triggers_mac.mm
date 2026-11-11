// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/lock_triggers.h"

#import <AppKit/AppKit.h>

#include <memory>
#include <utility>
#include <vector>

namespace pane {

namespace {

// The Mac's: its displays sleeping and another user taking the session, from
// NSWorkspace's own notifications, as the Swift's LockController heard them;
// Pane leaving the front, from NSApplication's.
class MacSignals : public LockTriggers::Signals {
 public:
  MacSignals(base::RepeatingClosure lock, base::RepeatingClosure left_front)
      : lock_(std::move(lock)), left_front_(std::move(left_front)) {
    NSNotificationCenter* workspace =
        NSWorkspace.sharedWorkspace.notificationCenter;
    for (NSNotificationName name :
         {NSWorkspaceScreensDidSleepNotification,
          NSWorkspaceSessionDidResignActiveNotification}) {
      Observe(workspace, name, lock_);
    }
    Observe(NSNotificationCenter.defaultCenter,
            NSApplicationDidResignActiveNotification, left_front_);
  }

  ~MacSignals() override {
    for (const auto& [center, observer] : observers_) {
      [center removeObserver:observer];
    }
  }

  // LockTriggers::Signals:
  bool InFront() const override { return NSApp.active; }

 private:
  void Observe(NSNotificationCenter* center,
               NSNotificationName name,
               base::RepeatingClosure closure) {
    id observer = [center addObserverForName:name
                                      object:nil
                                       queue:nil
                                  usingBlock:^(NSNotification*) {
                                    closure.Run();
                                  }];
    observers_.emplace_back(center, observer);
  }

  const base::RepeatingClosure lock_;
  const base::RepeatingClosure left_front_;
  std::vector<std::pair<NSNotificationCenter * __strong, id __strong>>
      observers_;
};

}  // namespace

// static
std::unique_ptr<LockTriggers::Signals> LockTriggers::Signals::Create(
    base::RepeatingClosure lock,
    base::RepeatingClosure left_front) {
  return std::make_unique<MacSignals>(std::move(lock), std::move(left_front));
}

}  // namespace pane
