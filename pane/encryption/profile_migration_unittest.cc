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
#include "base/files/pane_file_codec.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/hash/sha1.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
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

// Past the registered key, base::WriteFile seals (pane_file_codec.h).
void WriteRaw(const base::FilePath& path, std::string_view bytes) {
  base::File file(path,
                  base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  ASSERT_TRUE(file.WriteAtCurrentPosAndCheck(base::as_byte_span(bytes)));
}

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
    ASSERT_EQ(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr),
              SQLITE_OK)
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
      const base::FilePath store =
          profile_.AppendASCII("Local Storage/leveldb");
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

    const base::FilePath blob = profile_.AppendASCII(
        "IndexedDB/https_kept.example_0.indexeddb.blob/1/00");
    ASSERT_TRUE(base::CreateDirectory(blob));
    ASSERT_TRUE(base::WriteFile(blob.AppendASCII("2"),
                                kSecret + std::string(100000, 'b')));

    const base::FilePath opfs = profile_.AppendASCII("File System/000/t/00");
    ASSERT_TRUE(base::CreateDirectory(opfs));
    ASSERT_TRUE(base::WriteFile(opfs.AppendASCII("00000001"), kSecret));

    const base::FilePath caches = profile_.AppendASCII("Cache/Cache_Data");
    ASSERT_TRUE(base::CreateDirectory(caches));
    ASSERT_TRUE(
        base::WriteFile(caches.AppendASCII("0123456789abcdef_0"), kSecret));
    ASSERT_TRUE(base::CreateDirectory(cache_.AppendASCII("Cache/Cache_Data")));
    ASSERT_TRUE(base::WriteFile(
        cache_.AppendASCII("Cache/Cache_Data/0123456789abcdef_0"), kSecret));
    ASSERT_TRUE(base::CreateDirectory(profile_.AppendASCII("Sessions")));
    ASSERT_TRUE(
        base::WriteFile(profile_.AppendASCII("Sessions/Tabs_1"), kSecret));
    ASSERT_TRUE(
        base::WriteFile(profile_.AppendASCII("Visited Links"), kSecret));

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
      EXPECT_TRUE(db->Get(leveldb::ReadOptions(), "k199", &value).ok())
          << store;
      EXPECT_TRUE(value.starts_with(kSecret));
    }

    EXPECT_FALSE(base::PathExists(profile_.AppendASCII("IndexedDB")));

    const base::FilePath blob =
        profile_.AppendASCII("WebStorage/7/IndexedDB/indexeddb.blob/1/00/2");
    std::unique_ptr<disk_cache::CacheFile> file =
        storage::PaneOpenEncryptedFile(
            blob, base::File::FLAG_OPEN | base::File::FLAG_READ);
    ASSERT_TRUE(file->IsValid());
    EXPECT_EQ(file->GetLength(),
              static_cast<int64_t>(strlen(kSecret) + 100000));

    // Nothing plain is left, the extensions' code aside; no temporary file.
    base::FileEnumerator files(folder_, /*recursive=*/true,
                               base::FileEnumerator::FILES);

    for (base::FilePath path = files.Next(); !path.empty();
         path = files.Next()) {
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
      base::BindRepeating([](std::vector<double>* shares,
                             double share) { shares->push_back(share); },
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
  EXPECT_TRUE(base::PathExists(profile_.AppendASCII(
      "IndexedDB/https_kept.example_0.indexeddb.leveldb/CURRENT")));
  EXPECT_TRUE(base::PathExists(
      profile_.AppendASCII("Extensions/abc/1.0/manifest.json")));

  // And it migrates again.
  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));
  ExpectMigrated();
}

// What a migration before 0.18.2 did to a first party's store with a
// partitioned bucket beside it: a row for each.
void AddPartitionedBucket(const base::FilePath& quota, int64_t id) {
  sql::Database db(sql::DatabaseOptions(), sql::test::kTestTag);
  ASSERT_TRUE(db.Open(quota));
  sql::Statement add(db.GetUniqueStatement(
      "INSERT INTO buckets VALUES(?, "
      "'https://kept.example/^0https://top.example', 'kept.example', "
      "'_default', 0, 0, 0, 0, 0, 0, 1)"));
  add.BindInt64(0, id);
  ASSERT_TRUE(add.Run());
}

TEST_F(ProfileMigrationTest, AFirstPartyStoreGoesToItsOwnBucket) {
  WritePlainSqlite(profile_.AppendASCII("WebStorage/QuotaManager"),
                   "CREATE UNIQUE INDEX buckets_by_storage_key ON "
                   "buckets(storage_key, name);"
                   "INSERT INTO buckets VALUES(30, "
                   "'https://kept.example/^0https://top.example',"
                   " 'kept.example', '_default', 0, 0, 0, 0, 0, 0, 1)");

  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));
  EXPECT_TRUE(base::PathExists(profile_.AppendASCII(
      "WebStorage/7/IndexedDB/indexeddb.leveldb/CURRENT")));
  EXPECT_FALSE(base::PathExists(profile_.AppendASCII("WebStorage/30")));
}

class RepairTest : public ProfileMigrationTest {
 protected:
  void SetUp() override {
    ProfileMigrationTest::SetUp();
    archive_ = folder_.AppendASCII("SafetyCopy");
    ASSERT_TRUE(WriteSafetyCopy(profile_, archive_));
    ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));
    AddPartitionedBucket(profile_.AppendASCII("WebStorage/QuotaManager"), 30);
  }

  base::FilePath Bucket(int id) {
    return profile_.AppendASCII("WebStorage")
        .AppendASCII(base::NumberToString(id))
        .AppendASCII("IndexedDB");
  }

  // The store where 0.18.0 left it: in the third party's bucket.
  void MoveToThirdParty() {
    ASSERT_TRUE(base::CreateDirectory(Bucket(30)));
    ASSERT_TRUE(base::Move(Bucket(7).AppendASCII("indexeddb.leveldb"),
                           Bucket(30).AppendASCII("indexeddb.leveldb")));
    ASSERT_TRUE(base::Move(Bucket(7).AppendASCII("indexeddb.blob"),
                           Bucket(30).AppendASCII("indexeddb.blob")));
  }

  void ExpectStoreIn(int bucket) {
    leveldb_env::Options options;
    std::unique_ptr<leveldb::DB> db;
    const base::FilePath store =
        Bucket(bucket).AppendASCII("indexeddb.leveldb");
    ASSERT_TRUE(leveldb_env::OpenDB(options, store.AsUTF8Unsafe(), &db).ok());
    std::string value;
    EXPECT_TRUE(db->Get(leveldb::ReadOptions(), "k199", &value).ok());
    EXPECT_TRUE(value.starts_with(kSecret));
  }

  base::FilePath archive_;
};

TEST_F(RepairTest, AStoreInAThirdPartysBucketGoesToItsFirstParty) {
  MoveToThirdParty();
  ASSERT_TRUE(RepairMigratedProfile(profile_, archive_));
  ExpectStoreIn(7);
  EXPECT_TRUE(base::PathExists(Bucket(7).AppendASCII("indexeddb.blob/1/00/2")));
  EXPECT_FALSE(base::PathExists(Bucket(30).AppendASCII("indexeddb.leveldb")));

  // Again: nothing to do.
  ASSERT_TRUE(RepairMigratedProfile(profile_, archive_));
  ExpectStoreIn(7);
}

TEST_F(RepairTest, AStoreNestedInAThirdPartysGoesToItsFirstParty) {
  ASSERT_TRUE(
      base::CreateDirectory(Bucket(30).AppendASCII("indexeddb.leveldb")));
  ASSERT_TRUE(base::WriteFile(
      Bucket(30).AppendASCII("indexeddb.leveldb/CURRENT"), "the embed's"));
  ASSERT_TRUE(base::Move(
      Bucket(7).AppendASCII("indexeddb.leveldb"),
      Bucket(30).AppendASCII(
          "indexeddb.leveldb/https_kept.example_0.indexeddb.leveldb")));
  ASSERT_TRUE(RepairMigratedProfile(profile_, archive_));
  ExpectStoreIn(7);
  EXPECT_TRUE(
      base::PathExists(Bucket(30).AppendASCII("indexeddb.leveldb/CURRENT")));
}

TEST_F(RepairTest, WithAStoreInEachBucketBothStay) {
  MoveToThirdParty();
  const base::FilePath again = Bucket(7).AppendASCII("indexeddb.leveldb");
  ASSERT_TRUE(base::CreateDirectory(again));
  ASSERT_TRUE(
      base::WriteFile(again.AppendASCII("CURRENT"), "the site's since"));
  ASSERT_TRUE(RepairMigratedProfile(profile_, archive_));
  ExpectStoreIn(30);
  std::string current;
  ASSERT_TRUE(base::ReadFileToString(again.AppendASCII("CURRENT"), &current));
  EXPECT_EQ(current, "the site's since");
}

TEST_F(RepairTest, AStoreBornAfterTheMigrationIsTheThirdPartysOwn) {
  // The first party's store stayed in its bucket, then was cleared; the embed
  // made its own.
  ASSERT_TRUE(
      base::DeletePathRecursively(Bucket(7).AppendASCII("indexeddb.leveldb")));
  const base::FilePath own = Bucket(30).AppendASCII("indexeddb.leveldb");
  usleep(1000);
  ASSERT_TRUE(base::CreateDirectory(own));
  ASSERT_TRUE(base::WriteFile(own.AppendASCII("CURRENT"), "the embed's"));
  ASSERT_TRUE(RepairMigratedProfile(profile_, archive_));
  EXPECT_TRUE(base::PathExists(own.AppendASCII("CURRENT")));
  EXPECT_FALSE(base::PathExists(Bucket(7).AppendASCII("indexeddb.leveldb")));
}

TEST_F(RepairTest, NoCopyNothingMoves) {
  MoveToThirdParty();
  ASSERT_TRUE(base::DeleteFile(archive_));
  ASSERT_TRUE(RepairMigratedProfile(profile_, archive_));
  ExpectStoreIn(30);
}

TEST_F(RepairTest, APartitionedFileSystemSealedWholeGoesBackToAStream) {
  const base::FilePath folder =
      profile_.AppendASCII("WebStorage/30/FileSystem/000/t/00");
  ASSERT_TRUE(base::CreateDirectory(folder));
  const base::FilePath file = folder.AppendASCII("00000001");
  const std::string content = kSecret + std::string(10000, 'o');
  WriteRaw(file, SealWholeFile(*KeysFor(file), base::as_byte_span(content)));

  ASSERT_TRUE(RepairMigratedProfile(profile_, archive_));
  ASSERT_TRUE(RepairMigratedProfile(profile_, archive_));

  std::unique_ptr<disk_cache::CacheFile> read = storage::PaneOpenEncryptedFile(
      file, base::File::FLAG_OPEN | base::File::FLAG_READ);
  ASSERT_TRUE(read->IsValid());
  ASSERT_EQ(read->GetLength(), static_cast<int64_t>(content.size()));
  std::string back(content.size(), '\0');
  ASSERT_EQ(read->Read(0, base::as_writable_byte_span(back)), content.size());
  EXPECT_EQ(back, content);
}

TEST_F(ProfileMigrationTest, APartitionedFileSystemMigratesAsAStream) {
  const base::FilePath folder =
      profile_.AppendASCII("WebStorage/30/FileSystem/000/t/00");
  ASSERT_TRUE(base::CreateDirectory(folder));
  const base::FilePath file = folder.AppendASCII("00000001");
  WriteRaw(file, kSecret);
  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));

  std::unique_ptr<disk_cache::CacheFile> read = storage::PaneOpenEncryptedFile(
      file, base::File::FLAG_OPEN | base::File::FLAG_READ);
  ASSERT_TRUE(read->IsValid());
  EXPECT_EQ(read->GetLength(), static_cast<int64_t>(strlen(kSecret)));
}

TEST_F(ProfileMigrationTest, AMalformedDatabaseGoesAndTheProfileOpens) {
  const base::FilePath history = profile_.AppendASCII("History");
  std::string fill = "CREATE TABLE big(value TEXT);";

  for (int row = 0; row < 500; row++) {
    fill += "INSERT INTO big VALUES(hex(zeroblob(512)));";
  }

  WritePlainSqlite(history, fill);

  std::optional<std::string> raw = [&]() -> std::optional<std::string> {
    base::File file(history, base::File::FLAG_OPEN | base::File::FLAG_READ);
    std::string bytes(file.GetLength(), '\0');
    return file.Read(0, base::as_writable_byte_span(bytes)) == bytes.size()
               ? std::make_optional(bytes)
               : std::nullopt;
  }();
  ASSERT_TRUE(raw);

  for (size_t at = 4096 * 3; at < raw->size(); at += 4096) {
    (*raw)[at] = 0x7f;
  }

  WriteRaw(history, *raw);
  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));
  EXPECT_FALSE(base::PathExists(history));
  EXPECT_TRUE(base::PathExists(profile_.AppendASCII("Preferences")));
}

TEST_F(ProfileMigrationTest, ABadCacheEntryGoesAndTheProfileOpens) {
  const base::FilePath caches =
      profile_.AppendASCII("Service Worker/ScriptCache");
  ASSERT_TRUE(base::CreateDirectory(caches));
  std::string entry("\x30\x5c\x72\xa7\x1b\x6d\xfb\xfc", 8);
  entry += std::string(4, '\0');
  entry += std::string("\xff\xff\xff\x7f", 4);
  entry += std::string(8, '\0');
  const base::FilePath bad = caches.AppendASCII("0123456789abcdef_0");
  WriteRaw(bad, entry);
  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));
  EXPECT_FALSE(base::PathExists(bad));
}

TEST_F(ProfileMigrationTest, AFileTooLargeToSealWholeStays) {
  const base::FilePath large = profile_.AppendASCII("Large");
  base::File file(large,
                  base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  ASSERT_TRUE(file.SetLength(int64_t{5} << 29));
  file.Close();
  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));
  EXPECT_EQ(base::GetFileSize(large), int64_t{5} << 29);
}

TEST_F(ProfileMigrationTest, TwoOwnersOfAnOriginKeepTheirCacheStorages) {
  const base::FilePath root =
      profile_.AppendASCII("Service Worker/CacheStorage");
  const std::string origin = "https://kept.example/";
  const std::string index =
      std::string("\x12", 1) + static_cast<char>(origin.size()) + origin;
  const std::string identifier = "https_kept.example_0";

  for (const std::string& owned : {identifier, identifier + "-1"}) {
    const base::FilePath folder = root.AppendASCII(
        base::HexEncodeLower(base::SHA1Hash(base::as_byte_span(owned))));
    ASSERT_TRUE(base::CreateDirectory(folder));
    WriteRaw(folder.AppendASCII("index.txt"), index);
    WriteRaw(folder.AppendASCII("owner"), owned);
  }

  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));
  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));

  for (const std::string& owned : {identifier, identifier + "-1"}) {
    const std::optional<std::string> keyed = KeyedName(profile_, owned);
    ASSERT_TRUE(keyed);
    std::string read;
    ASSERT_TRUE(base::ReadFileToString(
        root.AppendASCII(*keyed).AppendASCII("owner"), &read));
    EXPECT_EQ(read, owned);
  }
}

TEST_F(ProfileMigrationTest, AStalePersistentStateGoesFromNetworkToo) {
  const base::FilePath network = profile_.AppendASCII("Network");
  ASSERT_TRUE(base::CreateDirectory(network));
  const base::FilePath stale = network.AppendASCII("Network Persistent State");
  WriteRaw(stale, kSecret);
  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));
  EXPECT_FALSE(base::PathExists(stale));
}

TEST_F(ProfileMigrationTest, ACopyCutShortLeavesTheProfileAsItWas) {
  const base::FilePath archive = folder_.AppendASCII("SafetyCopy");
  ASSERT_TRUE(WriteSafetyCopy(profile_, archive));
  ASSERT_TRUE(MigrateProfileFiles(profile_, cache_, base::DoNothing()));

  base::File cut(archive, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  ASSERT_TRUE(cut.SetLength(cut.GetLength() / 2));
  cut.Close();

  EXPECT_FALSE(RestoreSafetyCopy(profile_, archive));
  ExpectMigrated();
}

// Appending to a stream file, killed at any point: every append it finished is
// there, and the file reads to its end, over a tail a kill may have left
// between the writes of one append.
TEST_F(ProfileMigrationTest, KilledWhileAppendingAStreamReadsToItsEnd) {
  const base::FilePath file =
      profile_.AppendASCII("File System/000/t/00/appended");
  const base::FilePath told = root_.AppendASCII("told");

  for (int round = 0; round < 60; round++) {
    base::CommandLine command = base::GetMultiProcessTestChildBaseCommandLine();
    command.AppendSwitchPath("root", root_);
    command.AppendSwitchPath("file", file);
    command.AppendSwitchPath("told", told);
    base::Process child = base::SpawnMultiProcessTestChild(
        "PaneStreamAppended", command, base::LaunchOptions());
    ASSERT_TRUE(child.IsValid());

    for (int wait = 0; wait < 4000 && !base::PathExists(told); wait++) {
      usleep(500);
    }

    usleep(2000 + (round * 137) % 4000);
    kill(child.Pid(), SIGKILL);
    int code = 0;
    child.WaitForExit(&code);

    std::string said;
    ASSERT_TRUE(base::ReadFileToString(told, &said));
    int64_t length = 0;
    ASSERT_TRUE(base::StringToInt64(said.empty() ? "0" : said, &length));

    std::unique_ptr<disk_cache::CacheFile> read =
        storage::PaneOpenEncryptedFile(
            file, base::File::FLAG_OPEN | base::File::FLAG_READ);
    ASSERT_TRUE(read->IsValid());
    const int64_t size = read->GetLength();
    ASSERT_GE(size, length) << "round " << round;
    std::vector<uint8_t> back(size);
    ASSERT_EQ(read->Read(0, back), back.size()) << "round " << round;
    EXPECT_TRUE(std::ranges::all_of(back, [](uint8_t b) { return b == 'a'; }));
  }
}

MULTIPROCESS_TEST_MAIN(PaneStreamAppended) {
  base::CommandLine* command = base::CommandLine::ForCurrentProcess();
  base::AtExitManager at_exit;
  const base::FilePath root = command->GetSwitchValuePath("root");
  AddRoots({root, root.AppendASCII("Volume")});
  Key key;
  key.fill(5);
  RegisterKey(kID, key);
  network::enterprise_encryption::InstallPaneCacheHook();
  const base::FilePath file = command->GetSwitchValuePath("file");
  const base::FilePath told = command->GetSwitchValuePath("told");
  base::CreateDirectory(file.DirName());
  std::unique_ptr<disk_cache::CacheFile> out = storage::PaneOpenEncryptedFile(
      file, base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_READ |
                base::File::FLAG_WRITE);
  std::vector<uint8_t> record(5000, 'a');

  for (int64_t at = out->GetLength();; at += record.size()) {
    if (!out->WriteAndCheck(at, record)) {
      return 1;
    }

    base::WriteFile(told, base::NumberToString(at + record.size()));
  }
}

}  // namespace
}  // namespace pane::encryption
