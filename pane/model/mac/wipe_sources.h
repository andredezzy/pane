// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_WIPE_SOURCES_H_
#define PANE_MODEL_MAC_WIPE_SOURCES_H_

#include <optional>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "pane/model/mac/safe_storage.h"
#include "pane/model/panic_wipe.h"
#include "pane/model/panic_wipe_run.h"

namespace pane::model {

class VolumeTools;

// What the panic wipe takes on the Mac besides the data's own files (ADR
// 0042): the caches Chromium keeps apart under ~/Library/Caches, the
// Electron app's data the migration copied, and that app's key.

// The record's source, whether or not it is still there (a wipe cut short
// finds its staged copy beside it); nullopt with no record, or one naming a
// folder that holds the data directory or sits inside it.
std::optional<ElectronData> ElectronDataToWipe(
    const base::FilePath& data_directory,
    const base::FilePath& application_support,
    const base::FilePath& caches);

// Chromium keeps a profile's caches under ~/Library/Caches when the profile is
// under ~/Library/Application Support, at the same relative path
// (chrome_paths_mac.mm GetUserCacheDirectory). nullopt otherwise.
std::optional<base::FilePath> CacheDirectoryFor(
    const base::FilePath& data_directory,
    const base::FilePath& application_support,
    const base::FilePath& caches);

// Where the engine keeps the profiles' caches when apart from their data:
// under ~/Library/Caches, for a data root under ~/Library/Application Support
// (CacheDirectoryFor). nullopt anywhere else.
std::optional<base::FilePath> ProfileCacheDirectory(
    const base::FilePath& data_directory);

// The Electron app's data this Pane migrated from, which the wipe erases too
// (ElectronData).
std::optional<ElectronData> ElectronDataFor(
    const base::FilePath& data_directory);

// The Electron app's cookie key goes with its data, when macOS lets it go
// without asking: a prompt would ask whoever typed the wrong PINs. Its data
// erased, the key opens nothing anyway.
void ForgetElectronKey(const std::optional<ElectronData>& electron,
                       SafeStorage safe_storage);

// The Mac's part of a wipe of `data_directory`: its caches apart and the
// Electron app's data; at once, the store where an earlier Pane kept the
// proxy passwords and the volume's leftovers detached (its key is gone); off
// the launch, the Electron app's key.
WipeExtras MacWipeExtras(const base::FilePath& data_directory,
                         scoped_refptr<VolumeTools> tools,
                         SafeStorage safe_storage);

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_WIPE_SOURCES_H_
