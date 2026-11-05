// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_ENCRYPTION_HANDOVER_H_
#define PANE_ENCRYPTION_HANDOVER_H_

#include "pane/encryption/public/mojom/engine_keys.mojom.h"

namespace pane::encryption {

// The browser process's keys and roots, each key in a shared memory region of
// its own, for another process. Null while there are no roots, in a process Pane
// does not host.
mojom::EngineKeysPtr KeysForOtherProcess();

// What a process writes of a profile's files: the Storage Service has only
// LevelDB's, which needs nothing installed, and has a SQLite VFS of its own.
enum class Stores {
  kAll,
  kLevelDB,
};

// Takes them in another process, into its registry. Null changes nothing.
void ReceiveKeys(mojom::EngineKeysPtr keys, Stores stores);

// Sets up a process that writes a profile's files through SQLite and base's file
// functions. Safe to call again.
void InstallHooks();

}  // namespace pane::encryption

#endif  // PANE_ENCRYPTION_HANDOVER_H_
