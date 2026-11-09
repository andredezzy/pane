// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/bundle_identifier.h"

#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <utility>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/files/file_util.h"
#include "base/strings/strcat.h"
#include "base/strings/sys_string_conversions.h"
#include "pane/model/mac/file_operations.h"

namespace pane::model {

namespace {

// The former domain's window frames into `domain`, the app's own, then the
// former domain goes, as UserDefaults' removePersistentDomain(forName:) takes
// it: every key it holds set to nothing.
void MoveWindowFrames(std::string_view former, const std::string& domain) {
  const base::apple::ScopedCFTypeRef<CFStringRef> former_id =
      base::SysUTF8ToCFStringRef(former);
  const base::apple::ScopedCFTypeRef<CFStringRef> domain_id =
      base::SysUTF8ToCFStringRef(domain);
  const base::apple::ScopedCFTypeRef<CFDictionaryRef> values(
      CFPreferencesCopyMultiple(nullptr, former_id.get(),
                                kCFPreferencesCurrentUser,
                                kCFPreferencesAnyHost));
  if (!values) {
    return;
  }

  const size_t count = static_cast<size_t>(CFDictionaryGetCount(values.get()));
  std::vector<const void*> keys(count);
  std::vector<const void*> objects(count);
  CFDictionaryGetKeysAndValues(values.get(), keys.data(), objects.data());
  const base::apple::ScopedCFTypeRef<CFMutableArrayRef> former_keys(
      CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));

  for (size_t i = 0; i < count; ++i) {
    const CFStringRef key = base::apple::CFCast<CFStringRef>(keys[i]);
    if (!key) {
      continue;
    }
    CFArrayAppendValue(former_keys.get(), key);
    if (base::SysCFStringRefToUTF8(key).starts_with("NSWindow Frame ")) {
      CFPreferencesSetValue(key, objects[i], domain_id.get(),
                            kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
    }
  }
  CFPreferencesSynchronize(domain_id.get(), kCFPreferencesCurrentUser,
                           kCFPreferencesAnyHost);

  CFPreferencesSetMultiple(nullptr, former_keys.get(), former_id.get(),
                           kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
  CFPreferencesSynchronize(former_id.get(), kCFPreferencesCurrentUser,
                           kCFPreferencesAnyHost);
}

// What would not go, as "<path>: <why>".
void Remove(const base::FilePath& path, std::vector<std::string>& failures) {
  if (base::expected<void, std::string> removed = RemoveItem(path);
      !removed.has_value()) {
    failures.push_back(base::StrCat({path.value(), ": ", removed.error()}));
  }
}

}  // namespace

std::vector<std::string> MoveFormerData(
    const base::FilePath& application_support,
    const base::FilePath& caches,
    const std::optional<std::string>& preferences,
    std::string_view former,
    std::string_view pane) {
  const base::FilePath data = application_support.Append(pane);
  const base::FilePath former_data = application_support.Append(former);

  if (base::PathExists(data) || !base::PathExists(former_data)) {
    return {};
  }

  std::vector<std::string> failures;

  const std::array<std::pair<base::FilePath, base::FilePath>, 2> moves = {{
      {former_data, data},
      {caches.Append(former), caches.Append(pane)},
  }};
  for (const auto& [from, to] : moves) {
    if (!base::PathExists(from) || base::PathExists(to)) {
      continue;
    }
    if (base::expected<void, std::string> moved = MoveItem(from, to);
        !moved.has_value()) {
      failures.push_back(base::StrCat({from.value(), ": ", moved.error()}));
    }
  }

  if (preferences) {
    MoveWindowFrames(former, *preferences);
  }

  return failures;
}

std::vector<std::string> RemoveSessionCaches(
    const base::FilePath& library,
    const std::vector<std::string>& identifiers) {
  std::vector<std::string> failures;

  for (const std::string& identifier : identifiers) {
    const base::FilePath caches = library.Append("Caches").Append(identifier);
    const base::FilePath storages = library.Append("HTTPStorages");

    std::vector<base::FilePath> leftovers;
    for (std::string_view name :
         {"Cache.db", "Cache.db-shm", "Cache.db-wal", "fsCachedData"}) {
      leftovers.push_back(caches.Append(name));
    }
    leftovers.push_back(storages.Append(identifier));
    leftovers.push_back(
        storages.Append(base::StrCat({identifier, ".binarycookies"})));

    for (const base::FilePath& leftover : leftovers) {
      if (base::PathExists(leftover)) {
        Remove(leftover, failures);
      }
    }
  }

  return failures;
}

std::optional<base::FilePath> UserCacheDirectory() {
  const size_t length = confstr(_CS_DARWIN_USER_CACHE_DIR, nullptr, 0);
  if (length == 0) {
    return std::nullopt;
  }
  std::string buffer(length, '\0');
  if (confstr(_CS_DARWIN_USER_CACHE_DIR, buffer.data(), buffer.size()) == 0) {
    return std::nullopt;
  }
  buffer.resize(std::min(buffer.find('\0'), buffer.size()));
  return base::FilePath(buffer).StripTrailingSeparators();
}

std::vector<std::string> RemoveFormerUserCaches(
    const base::FilePath& user_caches,
    std::string_view former) {
  std::vector<std::string> failures;
  for (const std::string& name :
       {std::string(former), base::StrCat({former, ".helper"})}) {
    const base::FilePath folder = user_caches.Append(name);
    if (base::PathExists(folder)) {
      Remove(folder, failures);
    }
  }
  return failures;
}

}  // namespace pane::model
