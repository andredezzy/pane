// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_ENCRYPTION_WHOLE_FILE_H_
#define PANE_ENCRYPTION_WHOLE_FILE_H_

#include <stdint.h>

#include <optional>
#include <string>

#include "base/containers/span.h"
#include "pane/encryption/engine_keys.h"

namespace pane::encryption {

// A small file rewritten whole (Preferences, Bookmarks, TransportSecurity): a
// magic and version, a random nonce, then AES-256-GCM over the whole content.
// Nothing binds it to its path, since Chromium copies these files.
std::string SealWholeFile(const ProfileKeys& keys,
                          base::span<const uint8_t> plaintext);

bool IsSealedWholeFile(base::span<const uint8_t> stored);

// Opens a sealed file. nullopt when it does not authenticate: another key, or
// changed bytes.
std::optional<std::string> OpenWholeFile(const ProfileKeys& keys,
                                         base::span<const uint8_t> stored);

// Points base's file functions at the codec, for the profiles' protected paths
// (base/files/pane_file_codec.h). Idempotent.
void InstallWholeFileCodec();

}  // namespace pane::encryption

#endif  // PANE_ENCRYPTION_WHOLE_FILE_H_
