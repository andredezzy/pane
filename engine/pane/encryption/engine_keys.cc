// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/engine_keys.h"

#include <algorithm>
#include <map>
#include <string_view>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "third_party/boringssl/src/include/openssl/digest.h"
#include "third_party/boringssl/src/include/openssl/hkdf.h"
#include "third_party/boringssl/src/include/openssl/hmac.h"
#include "third_party/boringssl/src/include/openssl/mem.h"

namespace pane::encryption {

namespace {

struct Registry {
  base::Lock lock;
  std::vector<base::FilePath> roots;
  std::map<std::string, Key> engine_keys;
  std::map<std::string, ProfileKeys> keys;
};

Registry& GetRegistry() {
  static base::NoDestructor<Registry> registry;
  return *registry;
}

constexpr std::string_view kVolumeRootName = "Volume";

// A UUID, the only name Pane gives a profile's folder: what Chromium keeps
// beside them in the user data dir (Default, Safe Browsing) is not one.
bool IsProfileName(std::string_view name) {
  if (name.size() != 36) {
    return false;
  }

  for (size_t index = 0; index < name.size(); index++) {
    const char c = name[index];

    if (index == 8 || index == 13 || index == 18 || index == 23) {
      if (c != '-') {
        return false;
      }
    } else if (!base::IsHexDigit(c)) {
      return false;
    }
  }

  return true;
}

struct Located {
  std::string id;
  // The path inside the profile's folder.
  std::vector<std::string> inside;
};

// The profile `path` is in, under `root`, in the two shapes a root has:
// <root>/<id>/... for the links' roots, and, for the volume's mount point,
// <root>/<id>/(Profile|Cache)/...
std::optional<Located> LocateIn(const base::FilePath& root,
                                const base::FilePath& path,
                                const std::map<std::string, Key>& known) {
  base::FilePath relative;

  if (!root.AppendRelativePath(path, &relative)) {
    return std::nullopt;
  }

  std::vector<std::string> parts = relative.GetComponents();

  if (parts.empty()) {
    return std::nullopt;
  }

  // Under the user data dir, the volume is a root of its own.
  if (parts[0] == kVolumeRootName) {
    return std::nullopt;
  }

  const std::string id = parts[0];

  if (!IsProfileName(id) && !known.contains(id)) {
    return std::nullopt;
  }

  parts.erase(parts.begin());

  if (root.BaseName().value() == kVolumeRootName && !parts.empty() &&
      (parts[0] == "Profile" || parts[0] == "Cache")) {
    parts.erase(parts.begin());
  }

  return Located{id, std::move(parts)};
}

ProfileKeys DeriveProfileKeys(base::span<const uint8_t> engine_key) {
  const base::span<const uint8_t> no_salt;
  ProfileKeys keys;
  keys.sqlite_pages = DeriveKey(engine_key, no_salt, "Pane SQLite pages");
  keys.stream_files = DeriveKey(engine_key, no_salt, "Pane stream files");
  keys.stream_check = DeriveKey(engine_key, no_salt, "Pane stream check");
  keys.whole_files = DeriveKey(engine_key, no_salt, "Pane whole files");
  keys.caches = DeriveKey(engine_key, no_salt, "Pane caches");
  keys.names = DeriveKey(engine_key, no_salt, "Pane names");
  return keys;
}

}  // namespace

ProfileKeys::~ProfileKeys() {
  OPENSSL_cleanse(this, sizeof(*this));
}

Key DeriveKey(base::span<const uint8_t> key,
              base::span<const uint8_t> salt,
              std::string_view info) {
  Key derived;
  CHECK_EQ(HKDF(derived.data(), derived.size(), EVP_sha256(), key.data(),
                key.size(), salt.data(), salt.size(),
                reinterpret_cast<const uint8_t*>(info.data()), info.size()),
           1);
  return derived;
}

void AddRoots(const std::vector<base::FilePath>& roots) {
  Registry& registry = GetRegistry();
  base::AutoLock lock(registry.lock);

  for (const base::FilePath& root : roots) {
    // A relative root would match a relative path under the working directory.
    CHECK(root.IsAbsolute());

    if (!std::ranges::contains(registry.roots, root)) {
      registry.roots.push_back(root);
    }
  }
}

std::vector<base::FilePath> Roots() {
  Registry& registry = GetRegistry();
  base::AutoLock lock(registry.lock);
  return registry.roots;
}

void RegisterKey(const std::string& id,
                 base::span<const uint8_t, kKeySize> key) {
  Registry& registry = GetRegistry();
  base::AutoLock lock(registry.lock);
  base::span(registry.engine_keys[id]).copy_from(key);
  registry.keys[id] = DeriveProfileKeys(key);
}

std::vector<std::pair<std::string, Key>> RegisteredKeys() {
  Registry& registry = GetRegistry();
  base::AutoLock lock(registry.lock);
  return {registry.engine_keys.begin(), registry.engine_keys.end()};
}

size_t DropKeys() {
  Registry& registry = GetRegistry();
  base::AutoLock lock(registry.lock);
  const size_t held = registry.keys.size();

  for (auto& [id, key] : registry.engine_keys) {
    OPENSSL_cleanse(key.data(), key.size());
  }

  for (auto& [id, keys] : registry.keys) {
    OPENSSL_cleanse(&keys, sizeof(keys));
  }

  registry.engine_keys.clear();
  registry.keys.clear();
  return held;
}

size_t KeyCount() {
  Registry& registry = GetRegistry();
  base::AutoLock lock(registry.lock);
  return registry.keys.size();
}

PathVerdict Classify(const base::FilePath& path) {
  Registry& registry = GetRegistry();
  base::AutoLock lock(registry.lock);

  for (const base::FilePath& root : registry.roots) {
    std::optional<Located> located = LocateIn(root, path, registry.engine_keys);

    if (!located) {
      continue;
    }

    if (!located->inside.empty() && located->inside[0] == "Extensions") {
      return {PathKind::kAllowed, std::nullopt, {}};
    }

    auto key = registry.keys.find(located->id);

    // A protected path with no key: an error here would reach Chromium's
    // corruption handling, which deletes the store. A crash loses nothing.
    if (key == registry.keys.end()) {
      LOG(FATAL) << "A profile's file with no key.";
    }

    return {PathKind::kEncrypted, key->second, located->id};
  }

  return {};
}

std::optional<std::string> KeyedName(const base::FilePath& profile_path,
                                     std::string_view text) {
  // Any file of the profile: its folder itself is not a file of it.
  const PathVerdict verdict = Classify(profile_path.AppendASCII("Name"));

  if (verdict.kind != PathKind::kEncrypted) {
    return std::nullopt;
  }

  uint8_t digest[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  CHECK(HMAC(EVP_sha256(), verdict.keys->names.data(),
             verdict.keys->names.size(),
             reinterpret_cast<const uint8_t*>(text.data()), text.size(), digest,
             &length));
  return base::ToLowerASCII(base::HexEncode(base::span(digest).first(20u)));
}

void ResetForTesting() {
  DropKeys();
  Registry& registry = GetRegistry();
  base::AutoLock lock(registry.lock);
  registry.roots.clear();
}

}  // namespace pane::encryption
