// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/panic_wipe.h"

#include <unistd.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "pane/model/mac/bundle_identifier.h"
#include "pane/model/mac/electron_migration.h"
#include "pane/model/mac/file_operations.h"
#include "pane/model/mac/wipe_sources.h"
#include "pane/model/persisted_file.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's PanicWipeTests, in its SecurityTests.

std::vector<int> SortedPasses(const std::vector<StagedWipe>& staged) {
  std::vector<int> passes;
  for (const StagedWipe& wipe : staged) {
    passes.push_back(wipe.passes);
  }
  std::ranges::sort(passes);
  return passes;
}

void SaveRecord(const base::FilePath& data, std::string source) {
  ASSERT_TRUE(
      PersistedFile(data, kMigrationRecordName)
          .Save(
              MigrationRecord(std::move(source), "2026-10-28", {}).ToValue()));
}

// Erases the data and caches, and nothing beside them.
TEST(PanicWipeTest, Erases) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath root = directory.GetPath();
  const base::FilePath data = root.Append("data");
  const base::FilePath caches = root.Append("caches");
  const base::FilePath outside = root.Append("outside");
  const base::FilePath profile = data.Append("Engine").Append("Profile A");

  for (const base::FilePath& folder :
       {profile.Append("Local Storage"),
        caches.Append("Engine").Append("Profile A").Append("Cache"),
        data.Append("automation"), outside}) {
    ASSERT_TRUE(base::CreateDirectory(folder));
  }

  ASSERT_TRUE(base::WriteFile(profile.Append("Cookies"), "cookies"));
  ASSERT_TRUE(base::WriteFile(profile.Append("Local Storage").Append("leveldb"),
                              "storage"));
  ASSERT_TRUE(base::WriteFile(caches.Append("Engine")
                                  .Append("Profile A")
                                  .Append("Cache")
                                  .Append("data_0"),
                              "cache"));
  ASSERT_TRUE(base::WriteFile(outside.Append("kept.txt"), "kept"));
  ASSERT_TRUE(
      base::WriteFile(data.Append("automation").Append("kept.json"), "kept"));

  for (std::string_view name :
       {"profiles.json", "settings.json", "security.json"}) {
    ASSERT_TRUE(base::WriteFile(data.Append(name), "{}"));
  }

  // A link out of the data, as Chromium's SingletonSocket.
  ASSERT_TRUE(base::CreateSymbolicLink(
      outside, data.Append("Engine").Append("SingletonSocket")));

  // A second name for the cookies' bytes, outside the data: it shows they
  // were overwritten, not only unlinked.
  const base::FilePath witness = outside.Append("cookies-witness");
  EXPECT_EQ(
      link(profile.Append("Cookies").value().c_str(), witness.value().c_str()),
      0);

  // Staged: out of the app's way at once.
  EXPECT_TRUE(StageWipe(data, caches).empty());

  EXPECT_FALSE(base::PathExists(data.Append("Engine")));
  EXPECT_FALSE(base::PathExists(caches.Append("Engine")));

  for (std::string_view name :
       {"profiles.json", "settings.json", "security.json"}) {
    EXPECT_FALSE(base::PathExists(data.Append(name)));
  }

  EXPECT_EQ(SortedPasses(StagedWipes(data, caches)), (std::vector<int>{1, 3}));

  // Erased.
  EXPECT_TRUE(EraseStagedWipes(data, caches).empty());
  EXPECT_TRUE(StagedWipes(data, caches).empty());
  EXPECT_FALSE(base::PathExists(data.Append("Wiping")));
  EXPECT_FALSE(base::PathExists(caches.Append("Wiping")));
  std::string witnessed;
  ASSERT_TRUE(base::ReadFileToString(witness, &witnessed));
  EXPECT_NE(witnessed, "cookies");

  EXPECT_TRUE(base::PathExists(outside.Append("kept.txt")));
  EXPECT_TRUE(base::PathExists(data.Append("automation").Append("kept.json")));
}

// Erases the Electron app's data the migration names, and its caches.
TEST(PanicWipeTest, ErasesElectronData) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath root = directory.GetPath();
  const base::FilePath support =
      root.Append("Library").Append("Application Support");
  const base::FilePath caches = root.Append("Library").Append("Caches");
  const base::FilePath data = support.Append("Pane");
  const base::FilePath electron = support.Append("@pane/veil");
  const base::FilePath neighbor = support.Append("@pane/other");
  const base::FilePath electron_caches = caches.Append("@pane/veil");

  for (const base::FilePath& folder :
       {data, electron.Append("Partitions").Append("profile-a"),
        electron_caches.Append("Partitions")
            .Append("profile-a")
            .Append("Cache"),
        neighbor}) {
    ASSERT_TRUE(base::CreateDirectory(folder));
  }

  ASSERT_TRUE(base::WriteFile(electron.Append("profiles.json"), "{}"));
  ASSERT_TRUE(base::WriteFile(
      electron.Append("Partitions").Append("profile-a").Append("Cookies"),
      "cookies"));
  ASSERT_TRUE(base::WriteFile(electron_caches.Append("Partitions")
                                  .Append("profile-a")
                                  .Append("Cache")
                                  .Append("data_0"),
                              "cache"));
  ASSERT_TRUE(base::WriteFile(neighbor.Append("kept.txt"), "kept"));
  ASSERT_TRUE(PersistedFile(data, kMigrationRecordName)
                  .Save(MigrationRecord(electron.value(), "2026-10-28", {"a"})
                            .ToValue()));

  const std::optional<ElectronData> found =
      ElectronDataToWipe(data, support, caches);
  ASSERT_TRUE(found);
  EXPECT_EQ(found->directory, StandardizedPath(electron));
  EXPECT_EQ(found->cache_directory, electron_caches);

  EXPECT_TRUE(StageWipe(data, std::nullopt, found).empty());
  EXPECT_FALSE(base::PathExists(electron));
  EXPECT_FALSE(base::PathExists(electron_caches));

  // A wipe cut short: the next launch finds the staged copies from the
  // record, which stays.
  EXPECT_EQ(SortedPasses(StagedWipes(
                data, std::nullopt, ElectronDataToWipe(data, support, caches))),
            (std::vector<int>{1, 3}));

  EXPECT_TRUE(EraseStagedWipes(data, std::nullopt, found).empty());
  EXPECT_FALSE(base::PathExists(support.Append("@pane/Wiping")));
  EXPECT_FALSE(base::PathExists(caches.Append("@pane/Wiping")));
  EXPECT_TRUE(base::PathExists(neighbor.Append("kept.txt")));
  EXPECT_TRUE(base::PathExists(data.Append("migration.json")));
}

// A record naming anything but the Electron app's data erases nothing of it.
TEST(PanicWipeTest, RefusesOtherFolders) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath root = directory.GetPath();
  const base::FilePath support =
      root.Append("Library").Append("Application Support");
  const base::FilePath caches = root.Append("Library").Append("Caches");
  const base::FilePath data = support.Append("Pane");
  const base::FilePath documents = root.Append("Documents");
  ASSERT_TRUE(base::CreateDirectory(data.Append("Engine")));
  ASSERT_TRUE(base::CreateDirectory(documents));
  ASSERT_TRUE(base::WriteFile(documents.Append("kept.txt"), "kept"));

  // A folder without the Electron app's profiles.json stays.
  SaveRecord(data, documents.value());
  const std::optional<ElectronData> documents_data =
      ElectronDataToWipe(data, support, caches);
  ASSERT_TRUE(documents_data);
  EXPECT_TRUE(StageWipe(data, std::nullopt, documents_data).empty());
  EXPECT_TRUE(base::PathExists(documents.Append("kept.txt")));

  // The data directory's own folder, one holding it, or a shallow path: never
  // a source.
  for (const std::string& source :
       {data.value(), support.value(), std::string("/Users"),
        std::string("relative/path")}) {
    SaveRecord(data, source);
    EXPECT_EQ(ElectronDataToWipe(data, support, caches), std::nullopt)
        << source;
  }

  std::ignore = EraseStagedWipes(data, std::nullopt);
}

// A wipe cut short is still staged, and what it cannot erase is reported.
TEST(PanicWipeTest, CutShort) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath data = directory.GetPath().Append("data");
  const base::FilePath profile = data.Append("Engine").Append("Profile A");

  ASSERT_TRUE(base::CreateDirectory(profile));
  ASSERT_TRUE(base::WriteFile(profile.Append("Cookies"), "cookies"));
  EXPECT_TRUE(StageWipe(data, std::nullopt).empty());

  // The app quit before the erase: the next launch finds it.
  const std::vector<StagedWipe> staged = StagedWipes(data, std::nullopt);
  ASSERT_FALSE(staged.empty());
  const base::FilePath staged_profile =
      staged.front().path.Append("Engine").Append("Profile A");
  EXPECT_TRUE(base::PathExists(staged_profile.Append("Cookies")));

  // A folder it may not change: reported, not swallowed.
  base::ScopedClosureRunner writable(base::BindOnce(
      [](const base::FilePath& folder) {
        base::SetPosixFilePermissions(folder, 0755);
      },
      staged_profile));
  ASSERT_TRUE(base::SetPosixFilePermissions(staged_profile, 0555));
  const std::vector<WipeFailure> failures =
      EraseStagedWipes(data, std::nullopt);
  ASSERT_TRUE(base::SetPosixFilePermissions(staged_profile, 0755));

  EXPECT_TRUE(std::ranges::any_of(failures, [](const WipeFailure& failure) {
    return failure.path.ends_with("Profile A/Cookies");
  }));
  EXPECT_TRUE(EraseStagedWipes(data, std::nullopt).empty());
  EXPECT_FALSE(base::PathExists(data.Append("Wiping")));
}

// Caches live apart only under Application Support.
TEST(PanicWipeTest, CacheDirectory) {
  const base::FilePath support("/Users/someone/Library/Application Support");
  const base::FilePath caches("/Users/someone/Library/Caches");

  EXPECT_EQ(
      CacheDirectoryFor(support.Append(kBundleIdentifier), support, caches),
      caches.Append(kBundleIdentifier));
  EXPECT_EQ(
      CacheDirectoryFor(base::FilePath("/tmp/pane-data"), support, caches),
      std::nullopt);
  EXPECT_EQ(CacheDirectoryFor(support, support, caches), std::nullopt);
}

}  // namespace
}  // namespace pane::model
