// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_PROFILES_H_
#define PANE_BROWSER_PROFILES_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/functional/callback_forward.h"

class Profile;

namespace base {
class FilePath;
}

namespace pane {

// Whether `identifier` can name a profile: it becomes a directory under the
// engine's user data dir, so it may not name anything else, nor be longer
// than a file name can be (255 bytes on APFS).
bool IsValidProfileIdentifier(std::string_view identifier);

// The roots a profile's files live under, in each form a path reaches them,
// and its key, for this process and, through ContentBrowserClient, the
// others'. Before anything of the profile opens, in any process.
void RegisterEngineKey(const base::FilePath& user_data_dir,
                       const std::string& identifier,
                       base::span<const uint8_t> key);

// Opens, creating on first use, the profile `identifier`, every file it
// writes encrypted under `engine_key` (docs/adr/0121), then runs `done` with
// it, or with null when it can't: an identifier that can't be one, a key
// that isn't one, a folder that can't be made, or a profile that won't load.
void OpenProfile(const std::string& identifier,
                 base::span<const uint8_t> engine_key,
                 base::OnceCallback<void(Profile*)> done);

// What every Pane profile holds to, set as it opens: downloads without a
// dialog, device choosers and Chrome's password manager, autofill, sign-in,
// and Translate off.
void ApplyProfileDefaults(Profile* profile);

}  // namespace pane

#endif  // PANE_BROWSER_PROFILES_H_
