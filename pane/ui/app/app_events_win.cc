// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/app_events.h"

#include <memory>
#include <utility>

namespace pane {

namespace {

// What Windows hands Pane from outside its window comes as another launch of
// Pane, which the shell starts with a link another app opens, or with nothing
// as a shortcut or the taskbar starts Pane again: Chromium's process
// singleton hands it over (AppEvents), with no event of the system's own.
class AppEventsWin : public AppEvents {
 public:
  AppEventsWin(base::RepeatingClosure reopen, OpenURLs open_urls)
      : AppEvents(std::move(reopen), std::move(open_urls)) {}
};

}  // namespace

bool ClosingTheWindowQuits() {
  return true;
}

// static
std::unique_ptr<AppEvents> AppEvents::Listen(base::RepeatingClosure reopen,
                                             OpenURLs open_urls) {
  return std::make_unique<AppEventsWin>(std::move(reopen),
                                        std::move(open_urls));
}

}  // namespace pane
