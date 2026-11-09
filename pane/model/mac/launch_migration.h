// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_LAUNCH_MIGRATION_H_
#define PANE_MODEL_MAC_LAUNCH_MIGRATION_H_

#include <optional>

#include "base/files/file_path.h"
#include "pane/model/mac/electron_migration.h"
#include "pane/model/mac/safe_storage.h"

namespace pane::model {

class LaunchAlert;

// The first launch after the Electron app brings its data over (ADR 0043),
// before anything reads the data directory or the engine starts
// (MigrateElectronData), and its cookie key with it (SafeStorage::AdoptKey).
// The user hears of it through `alert`: a copy past a second shows a note, so
// the launch never looks hung, and while the Electron app runs, whose files may
// be mid-write, Pane waits for it to quit, or quits itself, having written
// nothing. Then the exit code it quits with is returned. Blocks, before the
// thread pool exists (docs/plans/views-migration.md, C5).
std::optional<int> RunLaunchMigration(const ElectronSource& source,
                                      const base::FilePath& data_directory,
                                      SafeStorage safe_storage,
                                      LaunchAlert& alert);

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_LAUNCH_MIGRATION_H_
