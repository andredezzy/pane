// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/app_events.h"

#include <memory>

namespace pane {

namespace {

// What Windows hands Pane from outside its window, a second launch's URLs
// through Chrome's process singleton among them, is W4's
// (docs/plans/views-migration.md): nothing is heard yet.
class AppEventsWin : public AppEvents {};

}  // namespace

// static
std::unique_ptr<AppEvents> AppEvents::Listen(base::RepeatingClosure reopen,
                                             OpenURLs open_urls) {
  return std::make_unique<AppEventsWin>();
}

}  // namespace pane
