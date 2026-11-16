// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_UPDATE_FOLDER_H_
#define PANE_INSTALLER_UPDATE_FOLDER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/version.h"

namespace pane::installer {

// Where an update waits on Windows (ADR 0127): beside the app folder, in the
// install's own folder and on its volume, so the helper swaps the two by
// renaming, as Pane and its helper both lay it out:
//
//   Programs\Pane\Application           the app, which Pane runs from
//   Programs\Pane\Update\<v>.partial    the payload unpacking, until whole
//   Programs\Pane\Update\<v>            staged: whole, its pane-version <v>
//   Programs\Pane\Update\<v>.download   the installer it came from, by path,
//                                       which goes once <v> is installed
//   Programs\Pane\Update\<v>.removing   a staged copy a newer offer passed,
//                                       on its way out
//   Programs\Pane\Update\Swapping       the old app, aside while the helper
//                                       swaps, to roll back to (Chromium's
//                                       MoveTreeWorkItem keeps it there)
//   Programs\Pane\Update\not-installed  the helper's mark: the last exit's
//                                       swap failed and rolled back (Paper
//                                       11.12.14, call E)

// The update folder beside `app_folder`.
base::FilePath UpdateFolderOf(const base::FilePath& app_folder);

base::FilePath StagedFolder(const base::FilePath& update_folder,
                            std::string_view version);
base::FilePath PartialFolder(const base::FilePath& update_folder,
                             std::string_view version);
base::FilePath RemovingFolder(const base::FilePath& update_folder,
                              std::string_view version);
base::FilePath DownloadRecord(const base::FilePath& update_folder,
                              std::string_view version);
base::FilePath SwappingFolder(const base::FilePath& update_folder);
base::FilePath NotInstalledMark(const base::FilePath& update_folder);

// Whether `version` names a folder of the update folder: a release's version,
// numbers and dots alone (base::Version's), so it can't name another path.
bool IsStagedVersion(std::string_view version);

// Whether `app_folder` is laid out as an install lays it out, a folder named
// Application in one named Pane, whose Update folder an update stages into.
bool IsInstallLayout(const base::FilePath& app_folder);

// The installer a staged version came from, as its record names it: nothing
// where none does, or the file is gone.
std::optional<base::FilePath> ReadDownloadRecord(const base::FilePath& record);
bool WriteDownloadRecord(const base::FilePath& record,
                         const base::FilePath& download);

// What a launch finds in the update folder (UpdateInstaller::
// RemoveLeftovers): the newest version staged whole and newer than the one
// running, which the next exit installs, and everything else Pane put there,
// which goes: an unpacking cut short, a staged copy a newer offer passed, an
// older or equal version (installed meanwhile, by the installer), a record
// without its folder, and the old app a swap left aside.
struct UpdateFolderScan {
  UpdateFolderScan();
  UpdateFolderScan(const UpdateFolderScan&);
  UpdateFolderScan& operator=(const UpdateFolderScan&);
  ~UpdateFolderScan();

  std::optional<std::string> kept;
  std::vector<base::FilePath> leftovers;
  // The installers the leftover versions came from: they go with them.
  std::vector<base::FilePath> leftover_downloads;
  // The helper left its mark.
  bool not_installed = false;
};

// Reads the update folder; changes nothing.
UpdateFolderScan ScanUpdateFolder(const base::FilePath& update_folder,
                                  const base::Version& running);

// What a launch keeps of the update folder (UpdateInstaller::
// RemoveLeftovers): the version ScanUpdateFolder keeps, the installer it
// came from, and whether the helper left its mark, which speaks for this
// launch alone and goes. Everything else goes, and the folder too where
// nothing is left in it. Blocks.
struct KeptUpdate {
  KeptUpdate();
  KeptUpdate(const KeptUpdate&);
  KeptUpdate& operator=(const KeptUpdate&);
  ~KeptUpdate();

  std::string version;
  base::FilePath staged;
  // Empty where no record names one.
  base::FilePath download;
  bool not_installed = false;
};
std::optional<KeptUpdate> CleanUpdateFolder(const base::FilePath& update_folder,
                                            const base::Version& running);

// Stages the installer at `installer`, offered as `version`, into
// `update_folder` (UpdateInstaller::Stage): its payload, read where it is,
// unpacked into <version>.partial, checked whole (its pane-version
// `version`, its chrome.exe and helper there), then renamed to <version>
// beside a record of the installer. The staged folder, or nothing, nothing
// of it left. Blocks for seconds.
std::optional<base::FilePath> StageUpdate(const base::FilePath& installer,
                                          std::string_view version,
                                          const base::FilePath& update_folder);

// A staged version a newer offer passed (ADR 0092), with its record and
// `download`: renamed out of the way first, so a quit before it has gone
// leaves what the next launch removes, never a staged copy it would keep.
void UnstageUpdate(const base::FilePath& staged,
                   const base::FilePath& download);

}  // namespace pane::installer

#endif  // PANE_INSTALLER_UPDATE_FOLDER_H_
