// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_BACKGROUND_WORK_H_
#define PANE_BROWSER_BACKGROUND_WORK_H_

#include "base/functional/callback_forward.h"
#include "base/time/time.h"

namespace pane {

// Runs `done` on the calling sequence, with true, once the thread pool has run
// every task it holds: those posted before the call, and what they post in
// turn; with false past `cap`. A lock's detach, and the keys' drop after it,
// wait for it (ADR 0116): Chrome posts work that reaches a profile's files as
// the profile is created and as it is destroyed, much of it BEST_EFFORT (the
// bounce-tracking database's open, the bookmarks' ordering written as the
// profile goes), which Chrome's startup or a busy Mac held past a lock's drop
// of the keys, and the encryption's check of a profile's file with no key
// ended Pane (ADR 0121). Waited for first, that work reaches the files with
// the volume there and the keys held. A marker posted after it would answer
// once it had begun, not ended. Chromium's own wait, which its tests use
// (ThreadPoolInstance::FlushAsyncForTesting).
void WaitForBackgroundWork(base::TimeDelta cap,
                           base::OnceCallback<void(bool ran)> done);

}  // namespace pane

#endif  // PANE_BROWSER_BACKGROUND_WORK_H_
