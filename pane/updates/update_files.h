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

// Opens `file` as OpenUpdateFile does, once this process has exited: the
// installer of an update Pane quits for. Opened while Pane quit, the dmg's
// DiskImageMounter, launched for Pane, could outlive it, and the system then
// counted it a helper of Pane's left running and added Pane to the Background
// Items, telling the user so (2026-11-12). The open waits in a shell of its
// own, as the installer's last step does (UpdateInstaller::Install).
void OpenUpdateFileAfterExit(const base::FilePath& file);

// OpenUpdateFileAfterExit's way, for a test: `opener` runs with `file` once
// process `pid` has exited. False when the shell didn't start.
bool OpenAfterProcessExits(int pid,
                           const base::FilePath& file,
                           const base::FilePath& opener);

// Takes `file` out of quarantine (ADR 0002): a dmg that matched the release's
// SHA-256, so the app copied from it opens without Gatekeeper's refusal.
// Pane quarantines every file it writes. Blocks.
bool ReleaseFromQuarantine(const base::FilePath& file);

}  // namespace pane

#endif  // PANE_UPDATES_UPDATE_FILES_H_
