// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_PROFILES_H_
#define PANE_BROWSER_PROFILES_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/functional/callback_forward.h"
#include "base/time/time.h"
#include "pane/model/profile.h"

class Profile;
class ScopedProfileKeepAlive;

namespace base {
class FilePath;
}

namespace pane {

// The roots a profile's files live under, in each form a path reaches them,
// and its key, for this process and, through ContentBrowserClient, the
// others'. Before anything of the profile opens, in any process. Thread-safe.
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

// A profile's fingerprint, or none (Chrome's own values), for the engine:
// before it opens, since its network context takes the User-Agent as it is
// made. False when the engine refused it.
bool SetProfileFingerprint(
    const std::string& identifier,
    const std::optional<model::Fingerprint>& fingerprint);

// What an open profile presents of its fingerprint through its preferences:
// the languages, Accept-Language with them, or Chrome's own without one.
void ApplyFingerprintLanguages(Profile* profile);

// The Mac's GPU as WebGL names it, for a fingerprint on the host's platform;
// nullopt before the GPU's info has come.
std::optional<model::Fingerprint::WebGL> HostWebGL();

// Every page of the profile closes at once, without beforeunload.
void CloseEveryPage(Profile* profile);

// Runs `done` with true once Chrome has destroyed `profile`; with false past
// `timeout`, or when the profile manager goes first.
void WaitForProfileDestruction(Profile* profile,
                               base::TimeDelta timeout,
                               base::OnceCallback<void(bool)> done);

// A profile closes in the engine for a lock (ADR 0116): its downloads under
// way stop as at a quit, `keep_alive` goes, every page closes without
// beforeunload, and its Browsers close, which Pane's otherwise keep open
// without tabs; Chrome then destroys it, which closes its databases. `done`
// says whether that came within 10 s.
void CloseProfile(Profile* profile,
                  std::unique_ptr<ScopedProfileKeepAlive> keep_alive,
                  base::OnceCallback<void(bool closed)> done);

// A profile's data goes in the engine (ADR 0020): `keep_alive`, if any,
// goes, every page closes at once without beforeunload, then Chrome closes
// the profile's Browsers, Pane's included, marks it for deletion, and removes
// its data, its folder once nothing holds the profile, or at the next start.
// `done` says whether Chrome marked it, which a quit first leaves undone. Never
// from inside a tab strip notification: it closes the profile's pages.
void DeleteProfile(Profile* profile,
                   std::unique_ptr<ScopedProfileKeepAlive> keep_alive,
                   base::OnceCallback<void(bool deleted)> done);

// Every process forgets every engine key, once a lock's detach has left no
// profile's file open (ADR 0121). `done` gets how many each still holds: the
// browser's, the network service's, and the Storage Service's.
void DropEngineKeys(
    base::OnceCallback<void(size_t browser, uint32_t network, uint32_t storage)>
        done);

// A profile's folder in the volume, `root` (resolved), converted in place by
// //pane/encryption's migration (ADR 0121), its key registered: a safety copy
// first, for a profile that has files. On a sequence that may block; a kill
// at any point loses nothing. `progress` gets the share done.
bool MigrateProfileFolder(const base::FilePath& root,
                          base::RepeatingCallback<void(double)> progress);

}  // namespace pane

#endif  // PANE_BROWSER_PROFILES_H_
