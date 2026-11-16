// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UPDATES_UPDATE_INSTALLER_H_
#define PANE_UPDATES_UPDATE_INSTALLER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"

namespace pane {

// Pane installing its own update (ADR 0092; the Swift's UpdateInstaller).
// Once the downloaded file checks out, the new version is staged beside the
// running app; at quit it swaps in, and the old app and the file go, and for
// Restart to update the new Pane opens. A step that fails leaves the running
// app as it was, and the user installs from the file. Each system's own:
//
// - The Mac's (update_installer_mac.cc): the Pane in the dmg is copied beside
//   the running bundle, hidden; at quit the two bundles swap in one rename,
//   so the app's path never stands empty, and a detached shell, once Pane has
//   exited, removes the old bundle and the dmg.
// - Windows' (update_installer_win.cc; ADR 0127): Pane reads the installer's
//   payload itself, never running it, into the update folder beside the app
//   folder (pane/installer/update_folder.h); at exit it starts the new
//   version's helper, copied out of the staged folder, which waits for
//   Pane's processes to go, swaps the two folders through Chromium's
//   WorkItemList, rolling back on failure, and removes what it replaced. A
//   swap that failed leaves the staged copy for the next exit, and a mark
//   the next launch reads (Paper 11.12.14, call E).
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

  // A staged update an earlier run left to install at the next exit:
  // Windows' alone, whose swap runs once Pane has gone and can fail, or wait
  // for a session that ended. The Mac's swap runs before Pane exits, and
  // what a quit left is a leftover.
  struct Kept {
    Kept();
    Kept(const Kept&);
    Kept& operator=(const Kept&);
    ~Kept();

    std::string version;
    base::FilePath staged;
    // The file it came from, which goes once it is installed; empty where
    // none is known.
    base::FilePath file;
    // The last exit's swap failed and rolled back: the launch says so above
    // the pill (Paper 11.12.14, E).
    bool not_installed = false;
  };

  // Nullopt when Pane cannot replace itself: on the Mac translocated, on a
  // disk image, or in a folder this user cannot write; on Windows anywhere
  // but its installed folder, and in a scenario's copy unless the scenario
  // says so. Then the user installs from the file (Quit and install, Exit
  // and install).
  static std::optional<UpdateInstaller> ForRunningApp();

  UpdateInstaller(const UpdateInstaller&);
  UpdateInstaller& operator=(const UpdateInstaller&);
  ~UpdateInstaller();

  // Stages the new version in `file`, Pane `version`: the copy, or nullopt.
  // `verified`: the file matched the release's SHA-256. The Mac stages
  // either, checking the copy's signature; Windows only a verified one: one
  // that wasn't keeps its Mark of the Web, and its installer runs at Exit and
  // install, where Windows asks first (Paper 11.12.11). Blocks for seconds:
  // it reads the whole app.
  std::optional<base::FilePath> Stage(const base::FilePath& file,
                                      const std::string& version,
                                      bool verified) const;

  // At quit: swaps `staged` in for the running app, or has the helper swap it
  // once Pane has exited (Windows). The old app and `file` go once Pane has
  // exited, and the new Pane opens when `relaunch` says how. False when the
  // swap failed, or its helper didn't start, the app as it was.
  bool Install(const base::FilePath& staged,
               const base::FilePath& file,
               const std::optional<Relaunch>& relaunch) const;

  // Removes what an earlier install left: a copy staged and never swapped
  // in, an old app the last step did not get to remove, a staging a quit cut
  // short, a dmg's mount point. On Windows it keeps a whole staged version
  // newer than `running_version`, which the next exit installs, and returns
  // it. Blocks.
  std::optional<Kept> RemoveLeftovers(std::string_view running_version) const;

  // The staged copy and its file, which a newer offer passed (ADR 0092): they
  // go, so no quit installs them. Blocks.
  void Unstage(const base::FilePath& staged, const base::FilePath& file) const;

 private:
  UpdateInstaller(base::FilePath app, std::string identifier);

  // The running app, which the update replaces: the bundle on the Mac, the
  // app folder on Windows; and on the Mac its identifier, which the new one
  // must have.
  base::FilePath app_;
  std::string identifier_;
};

}  // namespace pane

#endif  // PANE_UPDATES_UPDATE_INSTALLER_H_
