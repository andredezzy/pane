// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/bundle_identifier.h"

#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's BundleIdentifierTests and SessionCachesTests, in its
// DataVolumeTests.

base::apple::ScopedCFTypeRef<CFStringRef> Domain(const std::string& name) {
  return base::SysUTF8ToCFStringRef(name);
}

void Synchronize(const std::string& name) {
  CFPreferencesSynchronize(Domain(name).get(), kCFPreferencesCurrentUser,
                           kCFPreferencesAnyHost);
}

// UserDefaults' setPersistentDomain(_:forName:), for a domain of the test's
// own: the window's frame, and the open panel's last folder.
void SetFormerPreferences(const std::string& name) {
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> values(
      CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks));
  CFDictionarySetValue(values.get(), CFSTR("NSWindow Frame PaneMainWindow"),
                       CFSTR("0 0 1280 800"));
  const std::array<uint8_t, 1> folder = {1};
  base::apple::ScopedCFTypeRef<CFDataRef> data(CFDataCreate(
      kCFAllocatorDefault, folder.data(), static_cast<CFIndex>(folder.size())));
  CFDictionarySetValue(values.get(), CFSTR("NSOSPLastRootDirectory"),
                       data.get());
  CFPreferencesSetMultiple(values.get(), nullptr, Domain(name).get(),
                           kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
  Synchronize(name);
}

std::optional<std::string> StringPreference(const std::string& name,
                                            std::string_view key) {
  base::apple::ScopedCFTypeRef<CFPropertyListRef> value(
      CFPreferencesCopyAppValue(base::SysUTF8ToCFStringRef(key).get(),
                                Domain(name).get()));
  const CFStringRef string = base::apple::CFCast<CFStringRef>(value.get());
  return string ? std::make_optional(base::SysCFStringRefToUTF8(string))
                : std::nullopt;
}

// The keys a persistent domain holds.
size_t PreferenceCount(const std::string& name) {
  base::apple::ScopedCFTypeRef<CFDictionaryRef> values(
      CFPreferencesCopyMultiple(nullptr, Domain(name).get(),
                                kCFPreferencesCurrentUser,
                                kCFPreferencesAnyHost));
  return values ? static_cast<size_t>(CFDictionaryGetCount(values.get())) : 0;
}

// Each domain emptied, and the file cfprefsd leaves behind for each.
void ForgetPreferences(const std::vector<std::string>& names) {
  for (const std::string& name : names) {
    base::apple::ScopedCFTypeRef<CFArrayRef> keys(CFPreferencesCopyKeyList(
        Domain(name).get(), kCFPreferencesCurrentUser, kCFPreferencesAnyHost));
    if (keys) {
      CFPreferencesSetMultiple(nullptr, keys.get(), Domain(name).get(),
                               kCFPreferencesCurrentUser,
                               kCFPreferencesAnyHost);
    }
    Synchronize(name);
    base::DeleteFile(base::apple::GetUserLibraryPath()
                         .Append("Preferences")
                         .Append(base::StrCat({name, ".plist"})));
  }
}

std::vector<std::string> NamesIn(const base::FilePath& folder) {
  std::vector<std::string> names;
  base::FileEnumerator entries(folder, /*recursive=*/false,
                               base::FileEnumerator::FILES |
                                   base::FileEnumerator::DIRECTORIES |
                                   base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath entry = entries.Next(); !entry.empty();
       entry = entries.Next()) {
    names.push_back(entry.BaseName().value());
  }
  std::ranges::sort(names);
  return names;
}

// Identifiers of the test's own: a persistent domain is the whole Mac's, so
// the real ones would reach the installed Pane's preferences.
// An earlier Pane's data and caches move to this identifier's folders, once.
TEST(BundleIdentifierTest, MoveFormerData) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath root = directory.GetPath();
  const std::string unique =
      base::ToUpperASCII(base::Uuid::GenerateRandomV4().AsLowercaseString());
  const std::string former = base::StrCat({"pane-test-former-", unique});
  const std::string current = base::StrCat({"pane-test-current-", unique});
  base::ScopedClosureRunner forget(base::BindOnce(
      &ForgetPreferences, std::vector<std::string>{former, current}));

  const base::FilePath support = root.Append("Application Support");
  const base::FilePath caches = root.Append("Caches");
  ASSERT_TRUE(base::CreateDirectory(support.Append(former).Append("Engine")));
  ASSERT_TRUE(
      base::WriteFile(support.Append(former).Append("profiles.json"), "{}"));
  ASSERT_TRUE(base::CreateDirectory(caches.Append(former).Append("Engine")));
  SetFormerPreferences(former);

  const auto move = [&] {
    return MoveFormerData(support, caches, current, former, current);
  };

  EXPECT_TRUE(move().empty());
  EXPECT_TRUE(
      base::PathExists(support.Append(current).Append("profiles.json")));
  EXPECT_TRUE(base::PathExists(caches.Append(current).Append("Engine")));
  EXPECT_FALSE(base::PathExists(support.Append(former)));
  EXPECT_EQ(StringPreference(current, "NSWindow Frame PaneMainWindow"),
            "0 0 1280 800");
  EXPECT_EQ(PreferenceCount(former), 0u);

  // An earlier Pane's folder that turns up later moves nowhere: this
  // identifier's data is the one in use.
  ASSERT_TRUE(base::CreateDirectory(support.Append(former)));
  EXPECT_TRUE(move().empty());
  EXPECT_TRUE(base::PathExists(support.Append(former)));
}

// The former identifier's folders in the user's cache folder go, and nothing
// else there.
TEST(SessionCachesTest, FormerUserCaches) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath caches = directory.GetPath();

  for (std::string_view name :
       {"pane-old", "pane-old.helper", "pane-new", "pane-new.helper"}) {
    ASSERT_TRUE(
        base::CreateDirectory(caches.Append(name).Append("fsCachedData")));
    ASSERT_TRUE(base::WriteFile(caches.Append(name).Append("Cache.db"), "db"));
  }

  EXPECT_TRUE(RemoveFormerUserCaches(caches, "pane-old").empty());
  EXPECT_EQ(NamesIn(caches),
            (std::vector<std::string>{"pane-new", "pane-new.helper"}));
  // Nothing left to remove the next time.
  EXPECT_TRUE(RemoveFormerUserCaches(caches, "pane-old").empty());
  const std::optional<base::FilePath> user_caches = UserCacheDirectory();
  ASSERT_TRUE(user_caches);
  EXPECT_EQ(user_caches->BaseName().value(), "C");
}

// An earlier Pane's URL session caches go, under either identifier, and
// nothing else of its folders.
TEST(SessionCachesTest, Removed) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath library = directory.GetPath();

  for (std::string_view identifier : {"pane-a", "pane-b"}) {
    const base::FilePath caches = library.Append("Caches").Append(identifier);
    ASSERT_TRUE(base::CreateDirectory(caches.Append("fsCachedData")));
    ASSERT_TRUE(base::CreateDirectory(caches.Append("Engine")));
    ASSERT_TRUE(base::WriteFile(caches.Append("Cache.db"), "db"));
    ASSERT_TRUE(base::CreateDirectory(
        library.Append("HTTPStorages").Append(identifier)));
  }

  EXPECT_TRUE(RemoveSessionCaches(library, {"pane-a", "pane-b"}).empty());

  for (std::string_view identifier : {"pane-a", "pane-b"}) {
    const base::FilePath caches = library.Append("Caches").Append(identifier);
    EXPECT_FALSE(base::PathExists(caches.Append("Cache.db")));
    EXPECT_FALSE(base::PathExists(caches.Append("fsCachedData")));
    EXPECT_FALSE(
        base::PathExists(library.Append("HTTPStorages").Append(identifier)));
    EXPECT_TRUE(base::PathExists(caches.Append("Engine")));
  }
}

}  // namespace
}  // namespace pane::model
