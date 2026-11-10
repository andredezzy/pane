// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UPDATES_UPDATE_FILES_H_
#define PANE_UPDATES_UPDATE_FILES_H_

namespace base {
class FilePath;
}

namespace pane {

// The platform's part of an update's file.

// Opens `file` as a double-click would: the dmg's installer window.
void OpenUpdateFile(const base::FilePath& file);

// Takes `file` out of quarantine (ADR 0002): a dmg that matched the release's
// SHA-256, so the app copied from it opens without Gatekeeper's refusal.
// Pane quarantines every file it writes. Blocks.
bool ReleaseFromQuarantine(const base::FilePath& file);

}  // namespace pane

#endif  // PANE_UPDATES_UPDATE_FILES_H_
