// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UPDATES_UPDATE_INSTALLER_H_
#define PANE_UPDATES_UPDATE_INSTALLER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace pane {

// Pane installing its own update (ADR 0092; the Swift's UpdateInstaller).
// Once the dmg checks out, the Pane inside it is staged beside the running
// app, hidden; at quit the two bundles swap in one rename, so the app's path
// never stands empty, and a detached shell, once Pane has exited, removes the
// old bundle and the dmg and, for Restart to update, opens the new Pane. A
// step that fails leaves the running app as it was, and the user installs
// from the dmg. The platform's own: Windows has no install of its own yet
// (docs/plans/views-migration.md, W7), and says Pane cannot replace itself.
class UpdateInstaller {
 public:
  // How the new Pane opens after Restart to update.
  struct Relaunch {
    // In front, as the user's click asks, or behind the app in use, for a
    // scenario's copy that may not take the front (MayTakeFront).
    bool in_front = true;
    // The switches it opens with: a scenario's copy's mock keychain, so it
    // opens as one again, on the same throwaway data and with no Keychain
    // of the user's (ADR 0115); nothing for the user's own Pane.
    std::vector<std::string> switches;
  };

  // Nullopt when Pane cannot replace itself: translocated, on a disk image,
  // or in a folder this user cannot write. Then the dmg opens, and the user
  // drags Pane in (Quit and install).
  static std::optional<UpdateInstaller> ForRunningApp();

  UpdateInstaller(const UpdateInstaller&);
  UpdateInstaller& operator=(const UpdateInstaller&);
  ~UpdateInstaller();

  // Mounts the dmg, checks the Pane in it is this app at `version` with its
  // signature whole, and copies it beside the running one: the copy, or
  // nullopt. Blocks for seconds: it reads the whole app.
  std::optional<base::FilePath> Stage(const base::FilePath& dmg,
                                      const std::string& version) const;

  // At quit: swaps `staged` in for the running bundle in one rename. This
  // process keeps the files it has open; the shell waits for it to end
  // before it removes the old bundle and `dmg`, then opens the new Pane when
  // `relaunch` says how. False when the swap failed, the app as it was.
  bool Install(const base::FilePath& staged,
               const base::FilePath& dmg,
               const std::optional<Relaunch>& relaunch) const;

  // What an earlier install left: a copy staged and never swapped in, an
  // old bundle the shell did not get to remove, or the mount point of a Pane
  // that quit while staging, its dmg still attached there. Blocks.
  void RemoveLeftovers() const;

 private:
  UpdateInstaller(base::FilePath bundle, std::string identifier);

  // The running app's bundle, which the update replaces, and its identifier,
  // which the new one must have.
  base::FilePath bundle_;
  std::string identifier_;
};

}  // namespace pane

#endif  // PANE_UPDATES_UPDATE_INSTALLER_H_
