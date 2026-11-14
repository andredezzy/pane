// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/folder_place.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "pane/model/data_key.h"
#include "pane/model/data_lock.h"
#include "pane/model/data_vault.h"
#include "pane/model/engine_folder.h"
#include "pane/model/profile_deletions.h"
#include "pane/model/test_waits.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// The vault's sequences on Windows' place (U5): the engine's own folder, a
// profile's at Engine/<id>, nothing to attach, no earlier Pane's data.

constexpr char kProfile[] = "1F4A9C2E-7B3D-4E5F-8A6B-9C0D1E2F3A4B";

class Delegate : public DataVault::Delegate {
 public:
  void DataKeyDidOpen(base::OnceClosure loaded) override {
    std::move(loaded).Run();
  }
  std::vector<std::string> ProfileIds() override { return ids; }
  void AccessDidOpen() override { ++opened; }
  void VolumeWontOpen() override { ++wont_open; }
  void QuitToCloseVolume() override { ++quits; }
  void VolumeDidDetachAtLock() override { ++closed; }

  std::vector<std::string> ids;
  int opened = 0;
  int wont_open = 0;
  int quits = 0;
  int closed = 0;
};

class FolderPlaceTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    data_ = directory_.GetPath().AppendASCII("data");
    ASSERT_TRUE(base::CreateDirectory(EngineFolder(data_)));
  }

  void TearDown() override { task_environment_.RunUntilIdle(); }

  std::unique_ptr<DataVault> MakeVault(std::unique_ptr<DataLock> lock) {
    return std::make_unique<DataVault>(
        data_, base::MakeRefCounted<FolderPlace>(data_, std::move(lock)),
        &delegate_);
  }

  static void Open(DataVault& vault) {
    Key key;
    key.fill(5);
    vault.MakeAttacher().Run(key);
    vault.Open(key);
  }

  base::FilePath Folder(std::string_view id) const {
    return EngineFolder(data_).AppendASCII(id);
  }

  void MakeFolder(std::string_view id) {
    ASSERT_TRUE(base::CreateDirectory(Folder(id).AppendASCII("Network")));
    ASSERT_TRUE(base::WriteFile(
        Folder(id).AppendASCII("Network").AppendASCII("x"), "sealed"));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir directory_;
  base::FilePath data_;
  Delegate delegate_;
};

// The key opens the data at once: no volume, nothing moved.
TEST_F(FolderPlaceTest, OpensWithNoVolume) {
  std::unique_ptr<DataVault> vault = MakeVault(DataLock::Take(data_));
  std::optional<bool> attached;
  vault->WhenAttached(
      base::BindLambdaForTesting([&](bool result) { attached = result; }));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return attached.has_value(); }));
  EXPECT_EQ(attached, true);
  EXPECT_TRUE(vault->is_attached());
  EXPECT_EQ(vault->phase(), DataVault::Phase::kOpen);
  EXPECT_EQ(delegate_.wont_open, 0);
  EXPECT_FALSE(vault->failure());
}

// A profile deleted while the data was closed, or whose delete a quit cut
// short, goes once it opens, and its record with it.
TEST_F(FolderPlaceTest, PurgesTheDeletedOnceOpen) {
  MakeFolder(kProfile);
  ProfileDeletions(data_).Add(kProfile);
  std::unique_ptr<DataVault> vault = MakeVault(DataLock::Take(data_));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));
  EXPECT_FALSE(base::PathExists(Folder(kProfile)));
  EXPECT_TRUE(ProfileDeletions(data_).Pending().empty());
}

// A deleted profile's folder goes, everything under it, then its record; an
// id that is no folder's name removes nothing.
TEST_F(FolderPlaceTest, RemovesADeletedProfile) {
  MakeFolder(kProfile);
  MakeFolder("Default");
  std::unique_ptr<DataVault> vault = MakeVault(DataLock::Take(data_));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));

  bool recorded = false;
  vault->RecordDeletion(kProfile,
                        base::BindLambdaForTesting([&] { recorded = true; }));
  ASSERT_TRUE(WaitFor([&] { return recorded; }));
  EXPECT_EQ(ProfileDeletions(data_).Pending(),
            std::vector<std::string>{kProfile});

  bool removed = false;
  vault->Remove(kProfile, base::BindLambdaForTesting([&] { removed = true; }));
  ASSERT_TRUE(WaitFor([&] { return removed; }));
  EXPECT_FALSE(base::PathExists(Folder(kProfile)));
  EXPECT_TRUE(ProfileDeletions(data_).Pending().empty());

  bool refused = false;
  vault->Remove("..", base::BindLambdaForTesting([&] { refused = true; }));
  RunFor(base::Milliseconds(100));
  EXPECT_FALSE(refused);
  EXPECT_TRUE(base::PathExists(Folder("Default")));
}

// A file of the profile the engine still holds open (on Windows, where NTFS
// deletes no file open) holds the folder back only until it closes.
TEST_F(FolderPlaceTest, RemovesOnceTheEngineLetsGo) {
  MakeFolder(kProfile);
  std::unique_ptr<DataVault> vault = MakeVault(DataLock::Take(data_));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));

  auto held = std::make_unique<base::File>(
      Folder(kProfile).AppendASCII("Network").AppendASCII("x"),
      base::File::FLAG_OPEN | base::File::FLAG_READ);
  ASSERT_TRUE(held->IsValid());
  bool removed = false;
  vault->Remove(kProfile, base::BindLambdaForTesting([&] { removed = true; }));
  RunFor(base::Milliseconds(300));
  held.reset();
  ASSERT_TRUE(WaitFor([&] { return removed; }));
  EXPECT_FALSE(base::PathExists(Folder(kProfile)));
}

// A profile that saves no history opens without what it cached on disk while
// it did, as on the Mac; one that saves it keeps its caches.
TEST_F(FolderPlaceTest, OpensWithoutTheCachesOfAProfileThatSavesNoHistory) {
  MakeFolder(kProfile);
  const base::FilePath::CharType* const kCaches[] = {
      FILE_PATH_LITERAL("shared_proto_db"), FILE_PATH_LITERAL("Cache"),
      FILE_PATH_LITERAL("Code Cache")};
  for (const base::FilePath::CharType* item : kCaches) {
    ASSERT_TRUE(base::CreateDirectory(Folder(kProfile).Append(item)));
    ASSERT_TRUE(base::WriteFile(
        Folder(kProfile).Append(item).AppendASCII("entry"), "sealed"));
  }
  std::unique_ptr<DataVault> vault = MakeVault(DataLock::Take(data_));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));

  std::optional<bool> kept;
  vault->Prepare(kProfile, /*saves_history=*/true,
                 base::BindLambdaForTesting([&](bool done) { kept = done; }));
  ASSERT_TRUE(WaitFor([&] { return kept.has_value(); }));
  EXPECT_EQ(kept, true);
  for (const base::FilePath::CharType* item : kCaches) {
    EXPECT_TRUE(base::PathExists(Folder(kProfile).Append(item)));
  }

  std::optional<bool> cleared;
  vault->Prepare(
      kProfile, /*saves_history=*/false,
      base::BindLambdaForTesting([&](bool done) { cleared = done; }));
  ASSERT_TRUE(WaitFor([&] { return cleared.has_value(); }));
  EXPECT_EQ(cleared, true);
  for (const base::FilePath::CharType* item : kCaches) {
    EXPECT_FALSE(base::PathExists(Folder(kProfile).Append(item)));
  }
  EXPECT_TRUE(base::PathExists(
      Folder(kProfile).AppendASCII("Network").AppendASCII("x")));
}

// A lock closes the data once every profile has closed in the engine, and
// each process may forget its keys; one that would not close keeps them.
TEST_F(FolderPlaceTest, ALockClosesOnceEveryProfileHasClosed) {
  std::unique_ptr<DataVault> vault = MakeVault(DataLock::Take(data_));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));

  vault->Lock().Run(/*every_closed=*/false);
  RunFor(base::Milliseconds(100));
  EXPECT_EQ(delegate_.closed, 0);
  EXPECT_FALSE(vault->data_key());

  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));
  vault->Lock().Run(/*every_closed=*/true);
  ASSERT_TRUE(WaitFor([&] { return delegate_.closed == 1; }));
  EXPECT_EQ(delegate_.quits, 0);
  EXPECT_FALSE(vault->is_attached());
}

// A Pane started while another held the data waits for it at its first open,
// once that one lets go.
TEST_F(FolderPlaceTest, WaitsForTheDataLockAnotherPaneHeld) {
  std::unique_ptr<DataLock> other = DataLock::Take(data_);
  ASSERT_TRUE(other);
  std::unique_ptr<DataVault> vault = MakeVault(nullptr);
  Open(*vault);
  RunFor(base::Milliseconds(200));
  EXPECT_FALSE(vault->is_attached());

  other.reset();
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));
  EXPECT_FALSE(DataLock::Take(data_));
}

}  // namespace
}  // namespace pane::model
