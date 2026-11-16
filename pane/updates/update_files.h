// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UPDATES_UPDATE_FILES_H_
#define PANE_UPDATES_UPDATE_FILES_H_

namespace base {
class FilePath;
}

namespace pane {

// The platform's part of an update's file: the Mac's dmg, Windows' installer.

// Opens `file` as a double-click would: the dmg's installer window. Windows'
// installer opens only once Pane has exited (OpenUpdateFileAfterExit): opened
// while Pane runs, it would find Pane open.
void OpenUpdateFile(const base::FilePath& file);

// Opens `file` as OpenUpdateFile does, once this process has exited: the
// installer of an update Pane quits for (Quit and install, Exit and install).
// On the Mac, opened while Pane quit, the dmg's DiskImageMounter, launched
// for Pane, could outlive it, and the system then counted it a helper of
// Pane's left running and added Pane to the Background Items, telling the
// user so (2026-11-12): the open waits in a shell of its own, as the
// installer's last step does (UpdateInstaller::Install). Windows' installer
// is Pane's own, which waits for the Pane that opened it itself, hidden
// (pane/installer/switches.h's kAfterSwitch), and is opened as Explorer opens
// it, so one that kept its Mark of the Web meets SmartScreen first.
void OpenUpdateFileAfterExit(const base::FilePath& file);

// OpenUpdateFileAfterExit's way, for a test: on the Mac `opener` runs with
// `file` once process `pid` has exited; on Windows `opener`, or the installer
// `file` itself where it is empty, opens at once, told to wait for `pid`.
// False when nothing started.
bool OpenAfterProcessExits(int pid,
                           const base::FilePath& file,
                           const base::FilePath& opener);

// Takes `file` out of quarantine (ADR 0002): a file that matched the
// release's SHA-256, so the app copied from it opens without Gatekeeper's
// refusal, and Windows' installer without SmartScreen's question. Blocks.
bool ReleaseFromQuarantine(const base::FilePath& file);

// Marks `file` as come from the internet, a file the release's digest didn't
// vouch for: the Mac's needs nothing, Pane quarantining every file it writes;
// Windows' takes the Mark of the Web a download takes, the Internet zone and
// no address (patch 0008's), so SmartScreen asks before its installer runs.
// Blocks.
void Quarantine(const base::FilePath& file);

}  // namespace pane

#endif  // PANE_UPDATES_UPDATE_FILES_H_
