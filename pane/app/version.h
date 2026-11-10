// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_VERSION_H_
#define PANE_APP_VERSION_H_

#include <optional>
#include <string>

namespace pane {

// Pane's own version (app/VERSION, its one source), as a packaged bundle
// carries it (PaneVersion, which scripts/package.sh writes): what the update
// check compares the latest release against. A build that isn't packaged
// carries none and reads 0.0.0, the Swift app's fallback.
std::string PaneVersion();

// The platform's half: the version the bundle carries, if it carries one.
std::optional<std::string> BundledPaneVersion();

}  // namespace pane

#endif  // PANE_APP_VERSION_H_
