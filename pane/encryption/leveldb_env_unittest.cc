// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include <memory>
#include <string>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/gtest_util.h"
#include "base/test/task_environment.h"
#include "pane/encryption/engine_keys.h"
#include "pane/encryption/stream_cipher.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"
#include "third_party/leveldatabase/src/include/leveldb/write_batch.h"

namespace pane::encryption {
namespace {

constexpr char kID[] = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02";
constexpr char kSecret[] = "pane-secret-value-in-leveldb";

class LevelDBEnvTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_.CreateUniqueTempDir());
    ResetForTesting();
    root_ = base::MakeAbsoluteFilePath(temp_.GetPath());
    AddRoots({root_});
    Register(5);
    path_ = root_.AppendASCII(kID).AppendASCII("Store");
    ASSERT_TRUE(base::CreateDirectory(path_));
  }

  void TearDown() override { ResetForTesting(); }

  void Register(uint8_t seed) {
    Key key;
    key.fill(seed);
    RegisterKey(kID, key);
  }

  std::unique_ptr<leveldb::DB> Open() {
    leveldb_env::Options options;
    options.create_if_missing = true;
    options.paranoid_checks = true;
    std::unique_ptr<leveldb::DB> db;
    const leveldb::Status status =
        leveldb_env::OpenDB(options, path_.AsUTF8Unsafe(), &db);
    EXPECT_TRUE(status.ok()) << status.ToString();
    return db;
  }

  void Fill(leveldb::DB* db, int rows) {
    for (int i = 0; i < rows; i++) {
      ASSERT_TRUE(db->Put(leveldb::WriteOptions(), "key-" + std::to_string(i),
                          kSecret + std::string(500, 'v'))
                      .ok());
    }
  }

  // Every file of the store: none holds the value, or LevelDB's text log.
  void ExpectEncrypted() {
    base::FileEnumerator files(path_, /*recursive=*/false,
                               base::FileEnumerator::FILES);

    for (base::FilePath file = files.Next(); !file.empty();
         file = files.Next()) {
      std::string raw;
      ASSERT_TRUE(base::ReadFileToString(file, &raw));
      EXPECT_EQ(raw.find(kSecret), std::string::npos) << file;

      if (!raw.empty()) {
        EXPECT_TRUE(StreamCipher::HasMagic(base::as_byte_span(raw))) << file;
      }

      EXPECT_NE(file.BaseName().value(), FILE_PATH_LITERAL("LOG"));
    }
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_;
  base::FilePath root_;
  base::FilePath path_;
};

TEST_F(LevelDBEnvTest, WritesReadsAndCompactsEncrypted) {
  {
    std::unique_ptr<leveldb::DB> db = Open();
    ASSERT_TRUE(db);
    Fill(db.get(), 2000);
    db->CompactRange(nullptr, nullptr);
    Fill(db.get(), 10);
  }

  ExpectEncrypted();

  std::unique_ptr<leveldb::DB> db = Open();
  ASSERT_TRUE(db);
  std::string value;
  ASSERT_TRUE(db->Get(leveldb::ReadOptions(), "key-1999", &value).ok());
  EXPECT_TRUE(value.starts_with(kSecret));
  db.reset();
  ExpectEncrypted();
}

TEST_F(LevelDBEnvTest, RepairReadsLogicalSizes) {
  {
    std::unique_ptr<leveldb::DB> db = Open();
    ASSERT_TRUE(db);
    Fill(db.get(), 2000);
    db->CompactRange(nullptr, nullptr);
  }

  leveldb_env::Options options;
  ASSERT_TRUE(leveldb::RepairDB(path_.AsUTF8Unsafe(), options).ok());
  std::unique_ptr<leveldb::DB> db = Open();
  ASSERT_TRUE(db);
  std::string value;
  EXPECT_TRUE(db->Get(leveldb::ReadOptions(), "key-1000", &value).ok());
}

TEST_F(LevelDBEnvTest, AWrongKeyCrashesAndChangesNothing) {
  {
    std::unique_ptr<leveldb::DB> db = Open();
    ASSERT_TRUE(db);
    Fill(db.get(), 10);
  }

  std::string before;
  ASSERT_TRUE(base::ReadFileToString(path_.AppendASCII("CURRENT"), &before));
  Register(6);
  EXPECT_DEATH_IF_SUPPORTED(Open(), "");
  std::string after;
  ASSERT_TRUE(base::ReadFileToString(path_.AppendASCII("CURRENT"), &after));
  EXPECT_EQ(after, before);
}

TEST_F(LevelDBEnvTest, NoKeyCrashes) {
  DropKeys();
  EXPECT_DEATH_IF_SUPPORTED(Open(), "");
}

TEST_F(LevelDBEnvTest, APlainFileCrashes) {
  ASSERT_TRUE(
      base::WriteFile(path_.AppendASCII("CURRENT"), "MANIFEST-000001\n"));
  EXPECT_DEATH_IF_SUPPORTED(Open(), "");
}

TEST_F(LevelDBEnvTest, AFileCutShortAtItsHeaderCountsAsEmpty) {
  {
    std::unique_ptr<leveldb::DB> db = Open();
    ASSERT_TRUE(db);
    Fill(db.get(), 10);
  }

  // A new log a crash left with part of its header: LevelDB skips an empty log.
  ASSERT_TRUE(base::WriteFile(path_.AppendASCII("000099.log"), "PANE"));
  std::unique_ptr<leveldb::DB> db = Open();
  ASSERT_TRUE(db);
  std::string value;
  EXPECT_TRUE(db->Get(leveldb::ReadOptions(), "key-5", &value).ok());
}

}  // namespace
}  // namespace pane::encryption
