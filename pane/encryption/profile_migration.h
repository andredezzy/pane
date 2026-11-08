// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_ENCRYPTION_PROFILE_MIGRATION_H_
#define PANE_ENCRYPTION_PROFILE_MIGRATION_H_

#include <stdint.h>

#include "base/files/file_path.h"
#include "base/functional/callback.h"

namespace pane {

// Encrypts a closed profile's files, written by an earlier Pane, in place
// (docs/adr/0121), off the main thread, its key already registered. Every step
// works file by file, each written to a temporary file, verified by decrypting
// it, then renamed over the original, and skips what is already done: a kill at
// any point loses nothing, and the next run goes on.
//
// `profile` and `cache` are its folders in the volume, resolved. `progress`
// gets the share done, 0 to 1. False leaves what failed as it was.
bool MigrateProfileFiles(const base::FilePath& profile,
                         const base::FilePath& cache,
                         base::RepeatingCallback<void(double)> progress);

// An archive of what the migration will change, encrypted whole under the
// profile's key, for a failure in its first opens to restore. Atomic: a kill
// leaves none. False, with nothing written, when the disk would not hold it.
bool WriteSafetyCopy(const base::FilePath& profile,
                     const base::FilePath& archive);

// Puts the profile back as the archive has it: its files go, its extensions'
// code aside, and the archive's come back with their times. An archive that
// doesn't read through to its end leaves the profile as it was, and false.
bool RestoreSafetyCopy(const base::FilePath& profile,
                       const base::FilePath& archive);

// Puts right what a migration before 0.18.2 did to a closed profile: a site's
// IndexedDB moved into a bucket of a third party's, as the archive's listing
// shows, when that site has no store since; and partitioned file systems sealed
// as whole files. Each step can run again: false leaves the rest for the next
// call.
bool RepairMigratedProfile(const base::FilePath& profile,
                           const base::FilePath& archive);

}  // namespace pane

#endif  // PANE_ENCRYPTION_PROFILE_MIGRATION_H_
