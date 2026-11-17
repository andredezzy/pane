// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_ENCRYPTION_HANDOVER_H_
#define PANE_ENCRYPTION_HANDOVER_H_

#include <stdint.h>

#include "base/functional/callback_forward.h"
#include "base/time/time.h"
#include "pane/encryption/public/mojom/engine_keys.mojom.h"

namespace pane::encryption {

// The browser process's keys and roots, each key in a shared memory region of
// its own, for another process. Null while there are no roots, in a process
// Pane does not host.
mojom::EngineKeysPtr KeysForOtherProcess();

// Takes them in another process, into its registry. Null changes nothing.
void ReceiveKeys(mojom::EngineKeysPtr keys);

// The lock's drop in another process, the network service's or the Storage
// Service's, as the browser's waits before its own (ADR 0116): once this
// process's thread pool has run what the profiles' close left in it (the
// network service's last write of a closed profile's state, an
// ImportantFileWriter's), every key goes. Past 5 s they go all the same, and
// a write that comes later meets KeysFor's check, which ends the process: its
// restart loses that write alone, and writes nothing plain (ADR 0121). Keys an
// unlock registered meanwhile stay, and every other with them, since the
// profiles that unlock opens need them (DropKeysUnlessRegisteredSince). `done`
// gets how many keys the process holds once it has answered, on the calling
// sequence.
void DropKeysOnceBackgroundWorkRan(base::OnceCallback<void(uint32_t)> done);
// The same with its own cap, for tests.
void DropKeysOnceBackgroundWorkRan(base::TimeDelta cap,
                                   base::OnceCallback<void(uint32_t)> done);

// Sets up a process that writes a profile's files through SQLite and base's
// file functions. Safe to call again.
void InstallHooks();

}  // namespace pane::encryption

#endif  // PANE_ENCRYPTION_HANDOVER_H_
