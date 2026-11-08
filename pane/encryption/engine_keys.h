// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_ENCRYPTION_ENGINE_KEYS_H_
#define PANE_ENCRYPTION_ENGINE_KEYS_H_

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"

namespace pane::encryption {

inline constexpr size_t kKeySize = 32;
using Key = std::array<uint8_t, kKeySize>;

// What the formats use, each derived from the engine key, so that no two share
// a key.
struct ProfileKeys {
  ProfileKeys() = default;
  ProfileKeys(const ProfileKeys&) = default;
  ProfileKeys& operator=(const ProfileKeys&) = default;
  // Every copy is cleansed as it goes.
  ~ProfileKeys();

  Key sqlite_pages;
  Key stream_files;
  Key stream_check;
  Key whole_files;
  Key caches;
  Key names;
};

// A process's registry of its keys and of the roots that protected paths live
// under. Thread-safe. Nothing in it reaches the disk, a command line, or the
// environment.
//
// `roots` are the folders that hold profile folders, each in the form a path
// reaches it: the engine's user data dir and its caches' folder, with their
// links and resolved, and the volume's mount point. A profile's folder directly
// under a root is protected, whether it has a key here or not, so a path with
// none crashes the process: Chromium deletes or razes a store that fails to
// open, and a crash loses nothing.
void AddRoots(const std::vector<base::FilePath>& roots);
std::vector<base::FilePath> Roots();

// Registers a profile's engine key (idempotent). `id` is the name of its
// folder.
void RegisterKey(const std::string& id,
                 base::span<const uint8_t, kKeySize> key);

// The engine keys held, to pass on to another process.
std::vector<std::pair<std::string, Key>> RegisteredKeys();

// Forgets every key, once the lock's detach has left no file open. The roots
// stay: a path under them with no key still crashes. Returns how many it held.
size_t DropKeys();
size_t KeyCount();

// The keys of `path`, in either form, or nullopt for what stays plain: anything
// that is not a profile's file, and its extensions' public code. Crashes for a
// protected path with no key.
std::optional<ProfileKeys> KeysFor(const base::FilePath& path);

// A folder's name for `text` (an origin's identifier) in a profile's folder,
// keyed so that no list of sites gives it away: HMAC-SHA256 under the profile's
// names key, cut to 160 bits, in hex. nullopt outside a profile's folder.
std::optional<std::string> KeyedName(const base::FilePath& profile_path,
                                     std::string_view text);

// HKDF-SHA256 of `key`, with `salt` and `info`.
Key DeriveKey(base::span<const uint8_t> key,
              base::span<const uint8_t> salt,
              std::string_view info);

// For tests alone: forgets the roots and the keys.
void ResetForTesting();

}  // namespace pane::encryption

#endif  // PANE_ENCRYPTION_ENGINE_KEYS_H_
