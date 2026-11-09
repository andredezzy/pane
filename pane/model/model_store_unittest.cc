// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/model_store.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/simple_test_clock.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "pane/model/app_settings.h"
#include "pane/model/browser_model.h"
#include "pane/model/data_key.h"
#include "pane/model/persisted_file.h"
#include "pane/model/profile.h"
#include "pane/model/sealed_file.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// The Swift app's ModelStore had no tests of its own: its cases are the
// scenarios' (restore, proxy, security, identifier-move), which these check
// at the file's level.

// An earlier Pane's plain profiles.json: one proxy that signs in, its
// password kept apart, as 0.10 to 0.13 kept it (ProxyPasswordStore).
constexpr char kPlainProfiles[] = R"({"state":{"profiles":[{
  "id":"6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02","name":"Proxied","color":"ROSE",
  "group":null,"fingerprint":null,"keepLoaded":false,
  "proxy":{"proxyType":"HTTP","host":"proxy.example","port":8080,
           "username":"user","password":null},
  "tabs":[{"id":"t1","url":"https://kept-plain.example/","title":"Kept",
           "favicon":""}],
  "createdAt":"2026-10-30T00:00:00.000Z",
  "updatedAt":"2026-10-30T00:00:00.000Z"}]},"version":0})";

constexpr char kProxiedId[] = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02";

class ModelStoreTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(directory_.CreateUniqueTempDir()); }

  const base::FilePath& directory() const { return directory_.GetPath(); }

  base::FilePath FileNamed(const std::string& name) const {
    return directory().AppendASCII(name);
  }

  // A store whose files key is `key_` while `unlocked_`, with the passwords
  // an earlier Pane kept apart in `kept_apart_`, if any, in `folder` (the
  // test's own by default).
  std::unique_ptr<ModelStore> MakeStore(base::FilePath folder = {}) {
    if (folder.empty()) {
      folder = directory();
    }
    ModelStore::KeptApart kept_apart;
    kept_apart.read = base::BindLambdaForTesting(
        [this]() -> std::optional<ModelStore::Passwords> {
          ++reads_;
          return kept_apart_;
        });
    kept_apart.remove = base::BindLambdaForTesting([this] { ++removes_; });
    return std::make_unique<ModelStore>(
        folder, ModelStore::LoadSettings(folder), &clock_,
        base::BindLambdaForTesting([this]() -> std::optional<Key> {
          return unlocked_ ? std::make_optional(FilesKey(key_)) : std::nullopt;
        }),
        std::move(kept_apart));
  }

  static void Load(ModelStore& store) {
    base::RunLoop loaded;
    store.LoadProfiles(loaded.QuitClosure());
    loaded.Run();
  }

  static void Save(ModelStore& store) {
    base::RunLoop written;
    store.SaveNow(written.QuitClosure());
    written.Run();
  }

  std::string Read(const std::string& name) const {
    std::string text;
    base::ReadFileToString(FileNamed(name), &text);
    return text;
  }

  // profiles.json, opened as the next launch opens it.
  std::optional<std::vector<BrowserProfile>> Sealed() const {
    base::expected<std::optional<base::Value>, FileError> state =
        SealedFile(directory(), "profiles").Load(FilesKey(key_));
    if (!state.has_value() || !*state) {
      return std::nullopt;
    }
    return ProfilesFromState(**state);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::SimpleTestClock clock_;
  const Key key_ = MakeDataKey();
  bool unlocked_ = true;
  std::optional<ModelStore::Passwords> kept_apart_;
  int reads_ = 0;
  int removes_ = 0;

 private:
  base::ScopedTempDir directory_;
};

// Profiles and their tabs are sealed, nothing of them in the clear, and read
// back.
TEST_F(ModelStoreTest, SealsProfilesAndReadsThemBack) {
  std::string id;
  {
    std::unique_ptr<ModelStore> store = MakeStore();
    Load(*store);
    EXPECT_TRUE(store->model().profiles().empty());
    id = store->model().CreateProfile("Work", ProfileColor::kEmerald,
                                      std::nullopt);
    store->model().InsertTab({.id = "tab",
                              .url = "https://sealed.example/page",
                              .title = "Sealed title"},
                             id);
    Save(*store);
  }

  const std::string disk = Read("profiles.json");
  const std::optional<base::Value> box =
      base::JSONReader::Read(disk, base::JSON_PARSE_RFC);
  ASSERT_TRUE(box && box->is_dict());
  EXPECT_EQ(box->GetDict().size(), 1u);
  EXPECT_TRUE(box->GetDict().FindString("box"));
  for (const char* plain :
       {"sealed.example", "Sealed title", "Work", id.c_str()}) {
    EXPECT_EQ(disk.find(plain), std::string::npos) << plain;
  }

  std::unique_ptr<ModelStore> store = MakeStore();
  Load(*store);
  ASSERT_EQ(store->model().profiles().size(), 1u);
  const BrowserProfile& profile = store->model().profiles()[0];
  EXPECT_EQ(profile.id, id);
  EXPECT_EQ(profile.name, "Work");
  EXPECT_EQ(profile.color, ProfileColor::kEmerald);
  ASSERT_EQ(profile.tabs.size(), 1u);
  EXPECT_EQ(profile.tabs[0].url, "https://sealed.example/page");
  EXPECT_EQ(profile.tabs[0].title, "Sealed title");
}

// An earlier Pane's plain profiles.json is read once, with the passwords it
// kept apart, sealed at once with them, and their store goes.
TEST_F(ModelStoreTest, SealsAnEarlierPanesPlainFileOnce) {
  ASSERT_TRUE(base::WriteFile(FileNamed("profiles.json"), kPlainProfiles));
  kept_apart_ = ModelStore::Passwords{{kProxiedId, "kept-apart-secret"}};
  {
    std::unique_ptr<ModelStore> store = MakeStore();
    Load(*store);
    ASSERT_EQ(store->model().profiles().size(), 1u);
    EXPECT_EQ(store->model().profiles()[0].proxy->password,
              "kept-apart-secret");
    Save(*store);
  }
  EXPECT_EQ(reads_, 1);
  EXPECT_EQ(removes_, 1);

  const std::string disk = Read("profiles.json");
  EXPECT_EQ(disk.find("kept-plain"), std::string::npos);
  EXPECT_EQ(disk.find("kept-apart-secret"), std::string::npos);
  const std::optional<std::vector<BrowserProfile>> sealed = Sealed();
  ASSERT_TRUE(sealed && sealed->size() == 1u);
  EXPECT_EQ((*sealed)[0].proxy->password, "kept-apart-secret");
  EXPECT_EQ((*sealed)[0].tabs[0].url, "https://kept-plain.example/");

  // Sealed now: the store is never read again, nor removed again.
  std::unique_ptr<ModelStore> store = MakeStore();
  Load(*store);
  Save(*store);
  EXPECT_EQ(store->model().profiles()[0].proxy->password, "kept-apart-secret");
  EXPECT_EQ(reads_, 1);
  EXPECT_EQ(removes_, 1);
}

// Without a password kept apart that could be read, the profiles are sealed
// as they are, and no store goes.
TEST_F(ModelStoreTest, SealsAPlainFileWithoutItsStore) {
  ASSERT_TRUE(base::WriteFile(FileNamed("profiles.json"), kPlainProfiles));
  std::unique_ptr<ModelStore> store = MakeStore();
  Load(*store);
  Save(*store);
  EXPECT_EQ(reads_, 1);
  EXPECT_EQ(removes_, 0);
  const std::optional<std::vector<BrowserProfile>> sealed = Sealed();
  ASSERT_TRUE(sealed && sealed->size() == 1u);
  EXPECT_FALSE((*sealed)[0].proxy->password);
}

// A file that neither opens nor reads as plain is kept as .damaged before the
// first save writes over it; the first one kept stays.
TEST_F(ModelStoreTest, KeepsADamagedFileBeforeSaving) {
  for (const char* damaged : {"{not json", R"({"box":"AAAA"})"}) {
    SCOPED_TRACE(damaged);
    ASSERT_TRUE(base::DeleteFile(FileNamed("profiles.json.damaged")));
    ASSERT_TRUE(base::WriteFile(FileNamed("profiles.json"), damaged));
    std::unique_ptr<ModelStore> store = MakeStore();
    Load(*store);
    EXPECT_TRUE(store->model().profiles().empty());
    EXPECT_EQ(Read("profiles.json.damaged"), damaged);

    store->model().CreateProfile("After", ProfileColor::kBlue, std::nullopt);
    Save(*store);
    EXPECT_EQ(Read("profiles.json.damaged"), damaged);
    const std::optional<std::vector<BrowserProfile>> sealed = Sealed();
    ASSERT_TRUE(sealed && sealed->size() == 1u);
    EXPECT_EQ((*sealed)[0].name, "After");
  }
}

// A box sealed under another key doesn't open, and is kept too.
TEST_F(ModelStoreTest, KeepsAnotherKeysFile) {
  const Key other = MakeDataKey();
  ASSERT_TRUE(SealedFile(directory(), "profiles")
                  .Save(ProfilesState({}), FilesKey(other)));
  const std::string before = Read("profiles.json");
  std::unique_ptr<ModelStore> store = MakeStore();
  Load(*store);
  EXPECT_TRUE(store->model().profiles().empty());
  EXPECT_EQ(Read("profiles.json.damaged"), before);
}

// Before the key opens, or once a lock closes it, nothing is written over
// profiles.json; the settings still are.
TEST_F(ModelStoreTest, SavesNoProfilesWithoutTheKey) {
  {
    std::unique_ptr<ModelStore> store = MakeStore();
    Load(*store);
    store->model().CreateProfile("Kept", ProfileColor::kBlue, std::nullopt);
    Save(*store);
  }
  const std::string sealed = Read("profiles.json");

  unlocked_ = false;
  std::unique_ptr<ModelStore> store = MakeStore();
  Load(*store);
  EXPECT_FALSE(store->profiles_loaded());
  store->model().CreateProfile("Behind the lock", ProfileColor::kBlue,
                               std::nullopt);
  store->model().ChangeSettings(
      [](AppSettings& settings) { settings.theme = Theme::kDark; });
  Save(*store);
  EXPECT_EQ(Read("profiles.json"), sealed);
  EXPECT_EQ(ModelStore::LoadSettings(directory()).theme, Theme::kDark);
}

// With the key open but the profiles not loaded yet, the model holds none of
// them: a save then would lose every one.
TEST_F(ModelStoreTest, SavesNoProfilesBeforeTheyLoad) {
  {
    std::unique_ptr<ModelStore> store = MakeStore();
    Load(*store);
    store->model().CreateProfile("Kept", ProfileColor::kBlue, std::nullopt);
    Save(*store);
  }
  const std::string sealed = Read("profiles.json");
  std::unique_ptr<ModelStore> store = MakeStore();
  store->model().CreateProfile("Early", ProfileColor::kBlue, std::nullopt);
  Save(*store);
  EXPECT_EQ(Read("profiles.json"), sealed);
}

// A lock writes what changed, then the profiles leave the model; nothing
// after writes over them.
TEST_F(ModelStoreTest, UnloadSavesThenEmpties) {
  std::unique_ptr<ModelStore> store = MakeStore();
  Load(*store);
  const std::string id =
      store->model().CreateProfile("Locked", ProfileColor::kBlue, std::nullopt);
  store->UnloadProfiles();
  EXPECT_TRUE(store->model().profiles().empty());
  unlocked_ = false;
  task_environment_.FastForwardBy(BrowserModel::kSaveDelay * 2);
  Save(*store);

  const std::optional<std::vector<BrowserProfile>> sealed = Sealed();
  ASSERT_TRUE(sealed && sealed->size() == 1u);
  EXPECT_EQ((*sealed)[0].id, id);

  // The PIN opens them again.
  unlocked_ = true;
  Load(*store);
  ASSERT_EQ(store->model().profiles().size(), 1u);
  EXPECT_EQ(store->model().profiles()[0].name, "Locked");
}

// A lock that overtakes a load leaves the model empty, and the load's caller
// is never told.
TEST_F(ModelStoreTest, ALockOvertakesALoad) {
  {
    std::unique_ptr<ModelStore> store = MakeStore();
    Load(*store);
    store->model().CreateProfile("Kept", ProfileColor::kBlue, std::nullopt);
    Save(*store);
  }
  std::unique_ptr<ModelStore> store = MakeStore();
  bool loaded = false;
  store->LoadProfiles(base::BindLambdaForTesting([&] { loaded = true; }));
  store->UnloadProfiles();
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(loaded);
  EXPECT_TRUE(store->model().profiles().empty());
  EXPECT_FALSE(store->profiles_loaded());
}

// A write that fails is asked for again, the model's wait later.
TEST_F(ModelStoreTest, RetriesAFailedWrite) {
  std::unique_ptr<ModelStore> store = MakeStore();
  Load(*store);
  // A folder in the file's place: the rename over it fails.
  ASSERT_TRUE(base::CreateDirectory(FileNamed("profiles.json")));
  store->model().CreateProfile("Retried", ProfileColor::kBlue, std::nullopt);
  Save(*store);
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(FileNamed("profiles.json.damaged")));

  ASSERT_TRUE(base::DeletePathRecursively(FileNamed("profiles.json")));
  task_environment_.FastForwardBy(BrowserModel::kSaveDelay);
  Save(*store);
  const std::optional<std::vector<BrowserProfile>> sealed = Sealed();
  ASSERT_TRUE(sealed && sealed->size() == 1u);
  EXPECT_EQ((*sealed)[0].name, "Retried");
}

// A save whose text can't be made (no folder for it) is asked for again,
// the model's wait later.
TEST_F(ModelStoreTest, RetriesASaveThatCouldNotBeMade) {
  // A file where the store's folder's parent would be: no folder is made.
  const base::FilePath blocker = FileNamed("blocker");
  ASSERT_TRUE(base::WriteFile(blocker, "a file"));
  const base::FilePath folder = blocker.AppendASCII("data");
  std::unique_ptr<ModelStore> store = MakeStore(folder);
  Load(*store);
  store->model().CreateProfile("Asked again", ProfileColor::kBlue,
                               std::nullopt);
  Save(*store);
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(base::DeleteFile(blocker));
  task_environment_.FastForwardBy(BrowserModel::kSaveDelay);
  Save(*store);
  base::expected<std::optional<base::Value>, FileError> state =
      SealedFile(folder, "profiles").Load(FilesKey(key_));
  ASSERT_TRUE(state.has_value() && *state);
  const std::optional<std::vector<BrowserProfile>> profiles =
      ProfilesFromState(**state);
  ASSERT_TRUE(profiles && profiles->size() == 1u);
  EXPECT_EQ((*profiles)[0].name, "Asked again");
}

#if BUILDFLAG(IS_POSIX)
// A profiles.json the disk won't give is no damaged file: the model has no
// profiles, nothing is kept aside, and nothing is saved over it.
TEST_F(ModelStoreTest, SavesNothingOverAFileTheDiskWontGive) {
  {
    std::unique_ptr<ModelStore> store = MakeStore();
    Load(*store);
    store->model().CreateProfile("Kept", ProfileColor::kBlue, std::nullopt);
    Save(*store);
  }
  const std::string sealed = Read("profiles.json");
  ASSERT_TRUE(base::SetPosixFilePermissions(FileNamed("profiles.json"), 0));

  std::unique_ptr<ModelStore> store = MakeStore();
  Load(*store);
  EXPECT_FALSE(store->profiles_loaded());
  EXPECT_TRUE(store->model().profiles().empty());
  EXPECT_FALSE(base::PathExists(FileNamed("profiles.json.damaged")));
  store->model().CreateProfile("Not saved", ProfileColor::kBlue, std::nullopt);
  Save(*store);

  ASSERT_TRUE(base::SetPosixFilePermissions(FileNamed("profiles.json"), 0600));
  EXPECT_EQ(Read("profiles.json"), sealed);
}
#endif  // BUILDFLAG(IS_POSIX)

// settings.json that doesn't read is kept aside, and the defaults stand.
TEST_F(ModelStoreTest, KeepsDamagedSettings) {
  constexpr char kDamaged[] =
      R"({"state":{"settings":{"theme":"plaid"}},"version":0})";
  ASSERT_TRUE(base::WriteFile(FileNamed("settings.json"), kDamaged));
  EXPECT_EQ(ModelStore::LoadSettings(directory()), AppSettings());
  EXPECT_EQ(Read("settings.json.damaged"), kDamaged);

  std::unique_ptr<ModelStore> store = MakeStore();
  store->model().ChangeSettings(
      [](AppSettings& settings) { settings.theme = Theme::kLight; });
  Save(*store);
  EXPECT_EQ(ModelStore::LoadSettings(directory()).theme, Theme::kLight);
  EXPECT_EQ(Read("settings.json.damaged"), kDamaged);
}

// Settings are written plain, in the Electron app's envelope, every field
// present, null for off.
TEST_F(ModelStoreTest, WritesSettingsPlain) {
  std::unique_ptr<ModelStore> store = MakeStore();
  store->model().ChangeSettings([](AppSettings& settings) {
    settings.theme = Theme::kDark;
    settings.lock_after_minutes = std::nullopt;
  });
  Save(*store);
  const std::optional<base::Value> disk =
      base::JSONReader::Read(Read("settings.json"), base::JSON_PARSE_RFC);
  ASSERT_TRUE(disk && disk->is_dict());
  EXPECT_EQ(disk->GetDict().FindInt("version"), 0);
  const base::DictValue* settings =
      disk->GetDict().FindDictByDottedPath("state.settings");
  ASSERT_TRUE(settings);
  EXPECT_EQ(*settings->FindString("theme"), "dark");
  const base::Value* lock_after = settings->Find("lockAfterMinutes");
  ASSERT_TRUE(lock_after);
  EXPECT_TRUE(lock_after->is_none());
}

}  // namespace
}  // namespace pane::model
