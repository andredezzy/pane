// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_ENGINE_SECRET_H_
#define PANE_MODEL_ENGINE_SECRET_H_

#include <stdint.h>

#include <optional>
#include <vector>

namespace base {
class FilePath;
}

namespace pane::model {

// The engine's secret on Windows, which the data key is sealed under there
// (ADR 0113, U5), as the Keychain's "Pane Safe Storage" item is on the Mac
// (SafeStorage, pane/model/mac): os_crypt's key, 256 random bits in the
// engine's Local State under DPAPI ("os_crypt.encrypted_key", "DPAPI" and the
// blob, in base64), which the engine's cookies are sealed under too
// (components/os_crypt/async/browser/os_crypt_win.cc). DPAPI gives it to any
// process of the user's: the PIN's guard (PinGuard) is what a copy can't
// open. Windows' alone: built there.
//
// Found in `user_data_directory`'s Local State, or, where it has none and
// `makes_one`, made as Chromium makes it, before the engine first reads it: a
// fresh install needs the data key first, and Chromium then takes this one as
// its own. nullopt when DPAPI won't open it, or none is there to read. Under
// the mock keychain, a scenario's, the fixed secret every scenario's data is
// sealed under (kMockKeychainSecret), or none at all where the scenario says
// so (PANE_MOCK_KEYCHAIN_DENIES). Blocks on the disk.
std::optional<std::vector<uint8_t>> EngineSecret(
    const base::FilePath& user_data_directory,
    bool makes_one);

}  // namespace pane::model

#endif  // PANE_MODEL_ENGINE_SECRET_H_
