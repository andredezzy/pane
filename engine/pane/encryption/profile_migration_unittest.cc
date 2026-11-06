// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/profile_migration.h"

#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/files/pane_file_codec.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/test/multiprocess_test.h"
#include "base/test/task_environment.h"
#include "net/disk_cache/cache_file.h"
#include "net/disk_cache/pane_cache_hook.h"
#include "pane/encryption/engine_keys.h"
#include "pane/encryption/sqlite_vfs.h"
#include "pane/encryption/stream_cipher.h"
#include "pane/encryption/whole_file.h"
#include "services/network/enterprise/encryption/pane_cache_hook.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/test/test_helpers.h"
#include "storage/browser/file_system/pane_encrypted_stream.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/multiprocess_func_list.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"
#include "third_party/sqlite/sqlite3.h"

namespace pane::encryption {
namespace {

constexpr char kID[] = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02";
constexpr char kSecret[] = "pane-secret-value-in-a-profile";

class ProfileMigrationTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_.CreateUniqueTempDir());
    ResetForTesting();
    InstallSqliteVfs();
    root_ = base::MakeAbsoluteFilePath(temp_.GetPath());
    folder_ = root_.AppendASCII("Volume").Append(kID);
    profile_ = folder_.AppendASCII("Profile");
    cache_ = folder_.AppendASCII("Cache");
    ASSERT_TRUE(base::CreateDirectory(profile_));
    ASSERT_TRUE(base::CreateDirectory(cache_));
    WritePlainProfile();

    AddRoots({root_, root_.AppendASCII("Volume")});
    Key key;
    key.fill(5);
    RegisterKey(kID, key);
    InstallWholeFileCodec();
    network::enterprise_encryption::InstallPaneCacheHook();
  }

  void TearDown() override {
    base::SetPaneFileCodec(nullptr);
    disk_cache::SetPaneCacheHook(nullptr);
    ResetForTesting();
  }

  void WritePlainSqlite(const base::FilePath& path, const std::string& sql) {
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open_v2(path.value().c_str(), &db,
                              SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                              "VFSWrapper"),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr), SQLITE_OK)
        << sqlite3_errmsg(db);
    sqlite3_close(db);
  }

  // What an earlier Pane leaves: nothing registered yet, so all is plain.
  void WritePlainProfile() {
    ASSERT_TRUE(base::WriteFile(profile_.AppendASCII("Preferences"),
                                std::string("{\"a\":\"") + kSecret + "\"}"));
    WritePlainSqlite(profile_.AppendASCII("History"),
                     std::string("CREATE TABLE urls(url TEXT);"
                                 "INSERT INTO urls VALUES('") +
                         kSecret + "');");
    ASSERT_TRUE(base::CreateDirectory(profile_.AppendASCII("WebStorage")));
    WritePlainSqlite(
        profile_.AppendASCII("WebStorage/QuotaManager"),
        "CREATE TABLE buckets(id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " storage_key TEXT NOT NULL, host TEXT NOT NULL, name TEXT NOT NULL,"
        " use_count INTEGER NOT NULL, last_accessed INTEGER NOT NULL,"
        " last_modified INTEGER NOT NULL, expiration INTEGER NOT NULL,"
        " quota INTEGER NOT NULL, persistent INTEGER NOT NULL,"
        " durability INTEGER NOT NULL);"
        "INSERT INTO buckets VALUES(7, 'https://kept.example/', 'kept.example',"
        " '_default', 0, 0, 0, 0, 0, 0, 1);");

    {
      const base::FilePath store = profile_.AppendASCII("Local Storage/leveldb");
      ASSERT_TRUE(base::CreateDirectory(store));
      Fill(store);
    }

    const base::FilePath idb = profile_.AppendASCII(
        "IndexedDB/https_kept.example_0.indexeddb.leveldb");
    ASSERT_TRUE(base::CreateDirectory(idb));
    Fill(idb);
    const base::FilePath orphan = profile_.AppendASCII(
        "IndexedDB/https_lost.example_0.indexeddb.leveldb");
    ASSERT_TRUE(base::CreateDirectory(orphan));
    Fill(orphan);

    const base::FilePath blob =
        profile_.AppendASCII("IndexedDB/https_kept.example_0.indexeddb.blob/1/00");
    ASSERT_TRUE(base::CreateDirectory(blob));
    ASSERT_TRUE(base::WriteFile(blob.AppendASCII("2"),
                                kSecret + std::string(100000, 'b')));

    const base::FilePath opfs = profile_.AppendASCII("File System/000/t/00");
    ASSERT_TRUE(base::CreateDirectory(opfs));
    ASSERT_TRUE(base::WriteFile(opfs.AppendASCII("00000001"), kSecret));

    const base::FilePath caches = profile_.AppendASCII("Cache/Cache_Data");
    ASSERT_TRUE(base::CreateDirectory(caches));
    ASSERT_TRUE(base::WriteFile(caches.AppendASCII("0123456789abcdef_0"), kSecret));
    ASSERT_TRUE(base::CreateDirectory(cache_.AppendASCII("Cache/Cache_Data")));
    ASSERT_TRUE(base::WriteFile(
        cache_.AppendASCII("Cache/Cache_Data/0123456789abcdef_0"), kSecret));
    ASSERT_TRUE(base::CreateDirectory(profile_.AppendASCII("Sessions")));
    ASSERT_TRUE(base::WriteFile(profile_.AppendASCII("Sessions/Tabs_1"), kSecret));
    ASSERT_TRUE(base::WriteFile(profile_.AppendASCII("Visited Links"), kSecret));

    const base::FilePath code = profile_.AppendASCII("Extensions/abc/1.0");
    ASSERT_TRUE(base::CreateDirectory(code));
    ASSERT_TRUE(base::WriteFile(code.AppendASCII("manifest.json"), "{}"));
  }

  void Fill(const base::FilePath& store) {
    leveldb_env::Options options;
    options.create_if_missing = true;
    std::unique_ptr<leveldb::DB> db;
    ASSERT_TRUE(leveldb_env::OpenDB(options, store.AsUTF8Unsafe(), &db).ok());

    for (int i = 0; i < 200; i++) {
      ASSERT_TRUE(db->Put(leveldb::WriteOptions(), "k" + std::to_string(i),
                          std::string(kSecret) + std::string(400, 'v'))
                      .ok());
    }
  }

  void ExpectMigrated() {
    // Everything of the profile's is still there, read as the engine reads it.
    {
      sql::Database history(sql::DatabaseOptions(), sql::test::kTestTag);
      ASSERT_TRUE(history.Open(profile_.AppendASCII("History")));
      sql::Statement rows(history.GetUniqueStatement("SELECT url FROM urls"));
      ASSERT_TRUE(rows.Step());
      EXPECT_EQ(rows.ColumnString(0), kSecret);
    }

    std::string preferences;
    ASSERT_TRUE(base::ReadFileToString(profile_.AppendASCII("Preferences"),
                                       &preferences));
    EXPECT_NE(preferences.find(kSecret), std::string::npos);

    for (const base::FilePath& store :
         {profile_.AppendASCII("Local Storage/leveldb"),
          profile_.AppendASCII("WebStorage/7/IndexedDB/indexeddb.leveldb"),
          profile_.AppendASCII("WebStorage/8/IndexedDB/indexeddb.leveldb")}) {
      leveldb_env::Options options;
      std::unique_ptr<leveldb::DB> db;
      ASSERT_TRUE(leveldb_env::OpenDB(options, store.AsUTF8Unsafe(), &db).ok())
          << store;
      std::string value;
      EXPECT_TRUE(db->Get(leveldb::ReadOptions(), "k199", &value).ok()) << store;
      EXPECT_TRUE(value.starts_with(kSecret));
    }

    EXPECT_FALSE(base::PathExists(profile_.AppendASCII("IndexedDB")));

    const base::FilePath blob = profile_.AppendASCII(
        "WebStorage/7/IndexedDB/indexeddb.blob/1/00/2");
    std::unique_ptr<disk_cache::CacheFile> file = storage::PaneOpenEncryptedFile(
        blob, base::File::FLAG_OPEN | base::File::FLAG_READ);
    ASSERT_TRUE(file->IsValid());
    EXPECT_EQ(file->GetLength(),
              static_cast<int64_t>(strlen(kSecret) + 100000));

    // Nothing plain is left, the extensions' code aside; no temporary file.
    base::FileEnumerator files(folder_, /*recursive=*/true,
                               base::FileEnumerator::FILES);

    for (base::FilePath path = files.Next(); !path.empty(); path = files.Next()) {
      if (path.value().find("/Extensions/") != std::string::npos ||
          path.BaseName().value() == "SafetyCopy") {
        continue;
      }

      base::SetPaneFileCodec(nullptr);
      std::string raw;
      ASSERT_TRUE(base::ReadFileToString(path, &raw));
      InstallWholeFileCodec();
      EXPECT_EQ(raw.find(kSecret), std::string::npos) << path;
      EXPECT_EQ(path.value().find("pane-migrating"), std::string::npos) << path;
    }

    EXPECT_FALSE(base::PathExists(profile_.AppendASCII("Visited Links")));
    EXPECT_FALSE(base::PathExists(profile_.AppendASCII("Sessions")));
    EXPECT_FALSE(base::PathExists(profile_.AppendASCII("Cache")));
    EXPECT_TRUE(base::IsDirectoryEmpty(cache_));
    EXPECT_TRUE(base::PathExists(
        profile_.AppendASCII("Extensions/abc/1.0/manifest.json")));
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_;
  base::FilePath root_;
  base::FilePath folder_;
  base::FilePath profile_;
  base::FilePath cache_;
};

TEST_F(ProfileMigrationTest, EverythingIsEncryptedAndStillThere) {
  std::vector<double> shares;
  ASSERT_TRUE(MigrateProfileFiles(
      profile_, cache_,
      base::BindRepeating(
          [](std::vector<double>* shares, double share) {
            shares->push_back(share);
          },
          &shares)));
  EXPECT_EQ(shares.back(), 1);
  EXPECT_TRUE(std::ranges::is_sorted(shares));
  ExpectMigrated();

  // Again: nothing to do, nothing changes.
  EXPECT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));
  ExpectMigrated();
}

TEST_F(ProfileMigrationTest, KilledAtAnyPointItGoesOnAndLosesNothing) {
  const base::FilePath started = root_.AppendASCII("started");

  for (int round = 0; round < 30; round++) {
    base::CommandLine command = base::GetMultiProcessTestChildBaseCommandLine();
    command.AppendSwitchPath("root", root_);
    command.AppendSwitchPath("profile", profile_);
    command.AppendSwitchPath("cache", cache_);
    command.AppendSwitchPath("started", started);
    base::DeleteFile(started);
    base::Process child = base::SpawnMultiProcessTestChild(
        "PaneMigrationKilled", command, base::LaunchOptions());
    ASSERT_TRUE(child.IsValid());

    for (int wait = 0; wait < 2000 && !base::PathExists(started); wait++) {
      usleep(500);
    }

    usleep((round * 211) % 6000);
    kill(child.Pid(), SIGKILL);
    int code = 0;
    child.WaitForExit(&code);
  }

  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));
  ExpectMigrated();
}

MULTIPROCESS_TEST_MAIN(PaneMigrationKilled) {
  base::CommandLine* command = base::CommandLine::ForCurrentProcess();
  base::AtExitManager at_exit;
  const base::FilePath root = command->GetSwitchValuePath("root");
  AddRoots({root, root.AppendASCII("Volume")});
  Key key;
  key.fill(5);
  RegisterKey(kID, key);
  InstallSqliteVfs();
  InstallWholeFileCodec();
  network::enterprise_encryption::InstallPaneCacheHook();
  base::File(command->GetSwitchValuePath("started"),
             base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  MigrateProfileFiles(command->GetSwitchValuePath("profile"),
                      command->GetSwitchValuePath("cache"), base::DoNothing());
  return 0;
}

TEST_F(ProfileMigrationTest, ASafetyCopyPutsItAllBack) {
  const base::FilePath archive = folder_.AppendASCII("SafetyCopy");
  ASSERT_TRUE(WriteSafetyCopy(profile_, archive));

  std::string raw;
  ASSERT_TRUE(base::ReadFileToString(archive, &raw));
  EXPECT_EQ(raw.find(kSecret), std::string::npos);
  EXPECT_TRUE(StreamCipher::HasMagic(base::as_byte_span(raw)));

  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));
  ASSERT_TRUE(RestoreSafetyCopy(profile_, archive));

  // Plain again, as the earlier Pane left it, the code untouched.
  EXPECT_TRUE(IsPlainSqliteDatabase(profile_.AppendASCII("History")));
  EXPECT_TRUE(base::PathExists(
      profile_.AppendASCII("IndexedDB/https_kept.example_0.indexeddb.leveldb/CURRENT")));
  EXPECT_TRUE(base::PathExists(
      profile_.AppendASCII("Extensions/abc/1.0/manifest.json")));

  // And it migrates again.
  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));
  ExpectMigrated();
}

}  // namespace
}  // namespace pane::encryption
