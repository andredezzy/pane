// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_VERSION_H_
#define PANE_APP_VERSION_H_

#include <optional>
#include <string>

namespace pane {

// Pane's own version (packaging/VERSION, its one source), as a packaged app
// carries it (ADR 0044): on the Mac the bundle's PaneVersion, which
// scripts/package.sh writes, and on Windows the pane-version file beside
// chrome.dll, which scripts/package-win.sh writes. What the update check
// compares the latest release against, and About Pane shows. A build that
// isn't packaged carries none and reads 0.0.0, the Swift app's fallback.
std::string PaneVersion();

// The platform's half: the version the package carries, if it carries one.
std::optional<std::string> BundledPaneVersion();

}  // namespace pane

#endif  // PANE_APP_VERSION_H_
