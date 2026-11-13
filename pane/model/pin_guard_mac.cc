// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/pin_guard.h"

#include "base/files/file_path.h"

namespace pane::model {

// The Keychain guards the secret the data key is sealed under, asking the
// user before another app reads it (ADR 0113): the Mac's seals take no guard.
// static
scoped_refptr<PinGuard> PinGuard::ForData(
    const base::FilePath& data_directory) {
  return nullptr;
}

}  // namespace pane::model
