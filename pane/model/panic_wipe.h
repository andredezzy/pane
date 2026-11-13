// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PANIC_WIPE_H_
#define PANE_MODEL_PANIC_WIPE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/types/expected.h"

namespace base {
class File;
}

namespace pane::model {

// The panic wipe after the last failed PIN attempt (apps/veil
// security/wipe.ts): every profile's engine data and the app's own files are
// overwritten with random bytes, then deleted, and on the Mac so is the
// Electron app's data the migration copied them from. Nothing else is
// touched. The data moves out of the app's way first (StageWipe), at once, so
// a fresh Pane starts on nothing while the moved data is erased; a wipe cut
// short (a crash, a quit) is found and finished at the next launch. The order
// is one on every system; what each does to a file is its own
// (panic_wipe_mac.cc, panic_wipe_win.cc).

// The Electron app's data this Pane migrated from (its migration record's
// source), and that data's caches: every profile's cookies again, which the
// key the login Keychain keeps still opens. The user chose that the wipe
// erases them too (the migration itself only ever reads them).
// migration.json stays, so a wiped Pane never looks for them again.
struct ElectronData {
  friend bool operator==(const ElectronData&, const ElectronData&) = default;

  base::FilePath directory;
  std::optional<base::FilePath> cache_directory;
};

// What could not be erased, for the log and the security scenario.
struct WipeFailure {
  friend bool operator==(const WipeFailure&, const WipeFailure&) = default;

  // "<path>: <reason>".
  std::string ToString() const;

  std::string path;
  std::string reason;
};

// A folder of moved data, and how many random passes it gets: three for the
// profiles' data, one for caches, which hold nothing the profile does not.
struct StagedWipe {
  friend bool operator==(const StagedWipe&, const StagedWipe&) = default;

  base::FilePath path;
  int passes = 0;
};

// Where the moved data waits, beside each root on the same volume, so the
// move is a rename.
inline constexpr char kWipeStagingName[] = "Wiping";

// What it erases: the engine's data (every profile, extensions and all), the
// Mac's encrypted volume its profiles live in (ADR 0113), the app's JSON
// files, and the engine's caches when they live apart. security.json comes
// last, after its copy beside the volume: while it stays, with its count of
// failed attempts, the wipe is still due.
std::vector<base::FilePath> WipeTargets(
    const base::FilePath& data_directory,
    const std::optional<base::FilePath>& cache_directory,
    const std::optional<ElectronData>& electron = std::nullopt);

// Moves every target into `Wiping/<id>/` beside it, in WipeTargets's order.
// The Electron app's folder goes only while it still looks like its data (its
// profiles.json), so a record pointing elsewhere erases nothing.
std::vector<WipeFailure> StageWipe(
    const base::FilePath& data_directory,
    const std::optional<base::FilePath>& cache_directory,
    const std::optional<ElectronData>& electron = std::nullopt);

// What an earlier engine kept in the clear at its user data dir's root, which
// the engine keeps off now: the segmentation platform's database of the pages
// visited (ADR 0113), and the histograms that counted the pages loaded and
// the tabs open, with their spare and deferred files (ADR 0115). Staged for
// the erase at launch, before the engine starts.
std::vector<WipeFailure> StageEngineTraces(
    const base::FilePath& data_directory);

// Everything staged and not erased yet, this wipe's or one cut short's.
std::vector<StagedWipe> StagedWipes(
    const base::FilePath& data_directory,
    const std::optional<base::FilePath>& cache_directory,
    const std::optional<ElectronData>& electron = std::nullopt);

// Erases everything staged, then the staging folders once empty: a wipe
// staged meanwhile keeps its own.
std::vector<WipeFailure> EraseStagedWipes(
    const base::FilePath& data_directory,
    const std::optional<base::FilePath>& cache_directory,
    const std::optional<ElectronData>& electron = std::nullopt);

// Overwrites and deletes `path` and everything under it. A symbolic link goes,
// never what it points to: Chromium links its SingletonSocket into the
// temporary directory, and each profile's folder is a link into the volume.
// The volume's image goes without passes: the data key, gone first, was its
// only way in.
std::vector<WipeFailure> EraseForWipe(const base::FilePath& path,
                                      int passes = 3);

// What the wipe asks of each system's files: the Mac's (panic_wipe_mac.cc)
// as Foundation's FileManager did it, Windows' (panic_wipe_win.cc).
namespace wipe_files {

// What is at a path: the item itself, never what a link leads to.
enum class Kind { kMissing, kFolder, kFile, kOther };
base::expected<Kind, std::string> KindOf(const base::FilePath& path);

// What a folder's listing names (base::FileEnumerator's file types): its
// files and folders, and on the Mac its links as links, never followed.
int ListedTypes();

// The file's bytes on the disk itself, not a barrier: fsync(2) on the Mac,
// as FileHandle.synchronize did; FlushFileBuffers on Windows.
bool Sync(base::File& file);

// A rename beside the target, failing when something is at `to` already.
base::expected<void, std::string> Move(const base::FilePath& from,
                                       const base::FilePath& to);

// The item: a file, a link (never what it leads to), or a folder once empty.
base::expected<void, std::string> Remove(const base::FilePath& path);

// "<path>: <why>", why being the system's text for the call that just failed.
std::string LastError(const base::FilePath& path);

}  // namespace wipe_files

}  // namespace pane::model

#endif  // PANE_MODEL_PANIC_WIPE_H_
