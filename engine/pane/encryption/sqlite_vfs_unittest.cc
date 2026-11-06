// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/sqlite_vfs.h"

#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/test/multiprocess_test.h"
#include "base/functional/bind.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/gtest_util.h"
#include "pane/encryption/engine_keys.h"
#include "sql/database.h"
#include "sql/error_delegate_util.h"
#include "sql/recovery.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/multiprocess_func_list.h"
#include "third_party/sqlite/sqlite3.h"

namespace pane::encryption {
namespace {

constexpr char kID[] = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02";
constexpr char kSecret[] = "pane-secret-value-in-a-page";

sql::DatabaseOptions Options(bool exclusive = true) {
  return sql::DatabaseOptions().set_exclusive_locking(exclusive);
}

class SqliteVfsTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_.CreateUniqueTempDir());
    ResetForTesting();
    root_ = base::MakeAbsoluteFilePath(temp_.GetPath());
    AddRoots({root_});
    Key key;
    key.fill(5);
    RegisterKey(kID, key);
    profile_ = root_.Append(kID);
    ASSERT_TRUE(base::CreateDirectory(profile_));
    InstallSqliteVfs();
    path_ = profile_.Append("History");
  }

  void TearDown() override { ResetForTesting(); }

  std::string Raw(const base::FilePath& path) {
    std::string raw;
    EXPECT_TRUE(base::ReadFileToString(path, &raw));
    return raw;
  }

  void Fill(sql::Database& db, int rows, int first = 0) {
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE IF NOT EXISTS t(id INTEGER PRIMARY KEY, value TEXT)"));
    sql::Transaction transaction(&db);
    ASSERT_TRUE(transaction.Begin());
    sql::Statement insert(
        db.GetUniqueStatement("INSERT INTO t(id, value) VALUES(?, ?)"));

    for (int i = first; i < first + rows; i++) {
      insert.BindInt(0, i);
      insert.BindString(1, kSecret + std::string(200, 'x'));
      ASSERT_TRUE(insert.Run());
      insert.Reset(true);
    }

    ASSERT_TRUE(transaction.Commit());
  }

  int Count(sql::Database& db) {
    sql::Statement count(db.GetUniqueStatement("SELECT COUNT(*) FROM t"));
    return count.Step() ? count.ColumnInt(0) : -1;
  }

  base::ScopedTempDir temp_;
  base::FilePath root_;
  base::FilePath profile_;
  base::FilePath path_;
};

TEST_F(SqliteVfsTest, ANewDatabaseIsEncryptedAndReads) {
  {
    sql::Database db(Options(), sql::test::kTestTag);
    ASSERT_TRUE(db.Open(path_));
    Fill(db, 500);
    EXPECT_EQ(Count(db), 500);
  }

  EXPECT_TRUE(IsEncryptedSqliteDatabase(path_));
  EXPECT_FALSE(IsPlainSqliteDatabase(path_));
  EXPECT_EQ(Raw(path_).find(kSecret), std::string::npos);

  sql::Database again(Options(), sql::test::kTestTag);
  ASSERT_TRUE(again.Open(path_));
  EXPECT_EQ(Count(again), 500);

  sql::Statement check(again.GetUniqueStatement("PRAGMA integrity_check"));
  ASSERT_TRUE(check.Step());
  EXPECT_EQ(check.ColumnString(0), "ok");
}

TEST_F(SqliteVfsTest, EveryConnectionReservesItsBytes) {
  for (const std::string& name : {path_.value(), std::string(":memory:")}) {
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(name.c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "CREATE TABLE t(id)", nullptr, nullptr, nullptr),
              SQLITE_OK);
    int reserved = -1;
    ASSERT_EQ(sqlite3_file_control(db, "main", SQLITE_FCNTL_RESERVE_BYTES,
                                   &reserved),
              SQLITE_OK);
    EXPECT_EQ(reserved, kSqliteReservedBytes) << name;
    sqlite3_close(db);
  }
}

TEST_F(SqliteVfsTest, AFileOutsideAProfileStaysPlain) {
  const base::FilePath plain = root_.Append("Local Data");
  {
    sql::Database db(Options(), sql::test::kTestTag);
    ASSERT_TRUE(db.Open(plain));
    Fill(db, 10);
  }
  EXPECT_TRUE(IsPlainSqliteDatabase(plain));
  EXPECT_NE(Raw(plain).find(kSecret), std::string::npos);
}

TEST_F(SqliteVfsTest, ARollbackInEachJournalModeRestores) {
  for (const char* mode : {"TRUNCATE", "PERSIST", "DELETE"}) {
    const base::FilePath path = profile_.Append(std::string("Journal") + mode);
    sql::Database db(Options(), sql::test::kTestTag);
    ASSERT_TRUE(db.Open(path));
    ASSERT_TRUE(db.Execute(std::string("PRAGMA journal_mode=") + mode));
    Fill(db, 100);

    {
      sql::Transaction transaction(&db);
      ASSERT_TRUE(transaction.Begin());
      ASSERT_TRUE(db.Execute("DELETE FROM t"));
      EXPECT_EQ(Count(db), 0);
      transaction.Rollback();
    }

    EXPECT_EQ(Count(db), 100) << mode;
  }
}

TEST_F(SqliteVfsTest, WalWritesCheckpointsAndReopens) {
  for (const bool exclusive : {true, false}) {
    const base::FilePath path =
        profile_.Append(exclusive ? "WalExclusive" : "WalNormal");
    {
      sql::Database db(sql::DatabaseOptions()
                           .set_exclusive_locking(exclusive)
                           .set_wal_mode(true),
                       sql::test::kTestTag);
      ASSERT_TRUE(db.Open(path));
      Fill(db, 300);
      ASSERT_TRUE(db.CheckpointDatabase());
      Fill(db, 300, 300);
      EXPECT_EQ(Count(db), 600);
      const base::FilePath wal(path.value() + "-wal");

      if (base::PathExists(wal)) {
        EXPECT_EQ(Raw(wal).find(kSecret), std::string::npos);
      }
    }

    sql::Database again(sql::DatabaseOptions()
                            .set_exclusive_locking(exclusive)
                            .set_wal_mode(true),
                        sql::test::kTestTag);
    ASSERT_TRUE(again.Open(path));
    EXPECT_EQ(Count(again), 600);
    EXPECT_EQ(Raw(path).find(kSecret), std::string::npos);
  }
}

TEST_F(SqliteVfsTest, RazeLeavesAnEncryptedEmptyDatabase) {
  sql::Database db(Options(), sql::test::kTestTag);
  ASSERT_TRUE(db.Open(path_));
  Fill(db, 200);
  ASSERT_TRUE(db.Raze());
  EXPECT_FALSE(db.DoesTableExist("t"));
  Fill(db, 5);
  EXPECT_EQ(Count(db), 5);
  db.Close();
  EXPECT_TRUE(IsEncryptedSqliteDatabase(path_));
  EXPECT_EQ(Raw(path_).find(kSecret), std::string::npos);
}

TEST_F(SqliteVfsTest, VacuumKeepsItsPageSizeAndStaysEncrypted) {
  sql::Database db(Options(), sql::test::kTestTag);
  ASSERT_TRUE(db.Open(path_));
  Fill(db, 500);
  ASSERT_TRUE(db.Execute("DELETE FROM t WHERE id > 100"));
  ASSERT_TRUE(db.Execute("VACUUM"));
  EXPECT_EQ(Count(db), 101);

  for (const char* size : {"8192", "1024"}) {
    ASSERT_TRUE(db.Execute(std::string("PRAGMA page_size=") + size));
    ASSERT_TRUE(db.Execute("VACUUM"));
    EXPECT_EQ(Count(db), 101) << size;
    sql::Statement kept(db.GetUniqueStatement("PRAGMA page_size"));
    ASSERT_TRUE(kept.Step());
    EXPECT_EQ(kept.ColumnInt(0), 4096) << size;
  }

  db.Close();
  EXPECT_EQ(Raw(path_).find(kSecret), std::string::npos);

  sql::Database again(Options(), sql::test::kTestTag);
  ASSERT_TRUE(again.Open(path_));
  EXPECT_EQ(Count(again), 101);
}

TEST_F(SqliteVfsTest, ANewDatabaseTakesItsPageSize) {
  sql::Database db(Options().set_page_size(8192), sql::test::kTestTag);
  ASSERT_TRUE(db.Open(path_));
  Fill(db, 100);
  db.Close();
  EXPECT_EQ(Raw(path_).size() % 8192, 0u);
  sql::Database again(Options().set_page_size(8192), sql::test::kTestTag);
  ASSERT_TRUE(again.Open(path_));
  EXPECT_EQ(Count(again), 100);
}

TEST_F(SqliteVfsTest, RecoveryKeepsItsDataEncrypted) {
  sql::Database db(Options(), sql::test::kTestTag);
  ASSERT_TRUE(db.Open(path_));
  Fill(db, 50);
  ASSERT_EQ(sql::Recovery::RecoverDatabase(
                &db, sql::Recovery::Strategy::kRecoverOrRaze),
            sql::SqliteResultCode::kOk);
  ASSERT_FALSE(db.is_open());
  EXPECT_FALSE(IsPlainSqliteDatabase(path_));
  EXPECT_EQ(Raw(path_).find(kSecret), std::string::npos);

  sql::Database again(Options(), sql::test::kTestTag);
  ASSERT_TRUE(again.Open(path_));
  EXPECT_EQ(Count(again), 50);
}

TEST_F(SqliteVfsTest, AWrongKeyCrashesAndDeletesNothing) {
  {
    sql::Database db(Options(), sql::test::kTestTag);
    ASSERT_TRUE(db.Open(path_));
    Fill(db, 10);
  }

  const std::string before = Raw(path_);
  Key other;
  other.fill(6);
  RegisterKey(kID, other);

  EXPECT_DEATH_IF_SUPPORTED(
      {
        sql::Database db(Options(), sql::test::kTestTag);
        std::ignore = db.Open(path_);
      },
      "");
  EXPECT_EQ(Raw(path_), before);
}

TEST_F(SqliteVfsTest, NoKeyCrashesAndDeletesNothing) {
  {
    sql::Database db(Options(), sql::test::kTestTag);
    ASSERT_TRUE(db.Open(path_));
    Fill(db, 10);
  }

  const std::string before = Raw(path_);
  DropKeys();
  EXPECT_DEATH_IF_SUPPORTED(
      {
        sql::Database db(Options(), sql::test::kTestTag);
        std::ignore = db.Open(path_);
      },
      "");
  EXPECT_EQ(Raw(path_), before);
}

TEST_F(SqliteVfsTest, AChangedPageIsAnIOErrorAndNothingIsDeleted) {
  {
    sql::Database db(Options(), sql::test::kTestTag);
    ASSERT_TRUE(db.Open(path_));
    Fill(db, 500);
  }

  std::string raw = Raw(path_);
  raw[4096 * 5 + 100] ^= 1;
  ASSERT_TRUE(base::WriteFile(path_, raw));

  sql::Database db(Options(), sql::test::kTestTag);
  bool catastrophic = false;
  db.set_error_callback(base::BindRepeating(
      [](bool* catastrophic, int error, sql::Statement*) {
        *catastrophic = sql::IsErrorCatastrophic(error);
      },
      &catastrophic));
  ASSERT_TRUE(db.Open(path_));
  std::ignore = Count(db);
  sql::Statement scan(db.GetUniqueStatement("SELECT SUM(LENGTH(value)) FROM t"));
  std::ignore = scan.Step();
  EXPECT_FALSE(catastrophic);
  db.Close();
  EXPECT_EQ(Raw(path_), raw);
}

TEST_F(SqliteVfsTest, APlainDatabaseMigratesWithAllItsRows) {
  // Written plain, through Chromium's own VFS, as an earlier Pane wrote it.
  const base::FilePath plain = profile_.Append("Older");
  {
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open_v2(plain.value().c_str(), &db,
                              SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                              "VFSWrapper"),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db,
                           "CREATE TABLE t(id INTEGER PRIMARY KEY, value TEXT);"
                           "CREATE INDEX t_value ON t(value);"
                           "INSERT INTO t VALUES(1, 'pane-secret-value-in-a-page');"
                           "INSERT INTO t VALUES(2, 'pane-secret-value-in-a-page');",
                           nullptr, nullptr, nullptr),
              SQLITE_OK);
    sqlite3_close(db);
  }

  ASSERT_TRUE(IsPlainSqliteDatabase(plain));
  EXPECT_EQ(MigrateSqliteDatabase(plain), SqliteMigration::kMigrated);
  EXPECT_TRUE(IsEncryptedSqliteDatabase(plain));
  EXPECT_EQ(Raw(plain).find(kSecret), std::string::npos);
  EXPECT_EQ(MigrateSqliteDatabase(plain), SqliteMigration::kNothingToDo);

  sql::Database db(Options(), sql::test::kTestTag);
  ASSERT_TRUE(db.Open(plain));
  EXPECT_EQ(Count(db), 2);
  EXPECT_TRUE(db.DoesIndexExist("t_value"));
  Fill(db, 100, 10);
  EXPECT_EQ(Count(db), 102);
}

TEST_F(SqliteVfsTest, APlainDatabaseInAProfileCrashes) {
  const base::FilePath plain = profile_.Append("Older");
  {
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open_v2(plain.value().c_str(), &db,
                              SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                              "VFSWrapper"),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "CREATE TABLE t(id)", nullptr, nullptr, nullptr),
              SQLITE_OK);
    sqlite3_close(db);
  }

  EXPECT_DEATH_IF_SUPPORTED(
      {
        sql::Database db(Options(), sql::test::kTestTag);
        std::ignore = db.Open(plain);
      },
      "");
}

// The files as a crash leaves them mid-transaction, with pages it spilled to the
// main file or the WAL: a copy taken then, opened whole, shows the last commit.
TEST_F(SqliteVfsTest, ACrashMidTransactionRollsBackFromTheLogOnDisk) {
  for (const bool wal : {false, true}) {
    const base::FilePath path = profile_.Append(wal ? "HotWal" : "HotJournal");
    const base::FilePath copy = profile_.Append(wal ? "CopyWal" : "CopyJournal");
    sql::Database db(sql::DatabaseOptions().set_wal_mode(wal),
                     sql::test::kTestTag);
    ASSERT_TRUE(db.Open(path));
    Fill(db, 2000);
    ASSERT_TRUE(db.CheckpointDatabase() || !wal);
    ASSERT_TRUE(db.Execute("PRAGMA cache_size=5"));

    ASSERT_TRUE(db.Execute("BEGIN"));
    ASSERT_TRUE(db.Execute("UPDATE t SET value = 'pane-changed-' || id"));

    const std::string suffix = wal ? "-wal" : "-journal";
    const base::FilePath log(path.value() + suffix);
    ASSERT_TRUE(base::PathExists(log));
    std::string log_bytes = Raw(log);
    EXPECT_GT(log_bytes.size(), 4096u);
    EXPECT_EQ(log_bytes.find(kSecret), std::string::npos);
    EXPECT_EQ(log_bytes.find("pane-changed"), std::string::npos);

    ASSERT_TRUE(base::CopyFile(path, copy));
    ASSERT_TRUE(base::CopyFile(log, base::FilePath(copy.value() + suffix)));

    {
      sql::Database opened(sql::DatabaseOptions().set_wal_mode(wal),
                           sql::test::kTestTag);
      ASSERT_TRUE(opened.Open(copy));
      EXPECT_EQ(Count(opened), 2000);
      sql::Statement changed(opened.GetUniqueStatement(
          "SELECT COUNT(*) FROM t WHERE value LIKE 'pane-changed%'"));
      ASSERT_TRUE(changed.Step());
      EXPECT_EQ(changed.ColumnInt(0), 0) << (wal ? "wal" : "journal");
      sql::Statement check(opened.GetUniqueStatement("PRAGMA integrity_check"));
      ASSERT_TRUE(check.Step());
      EXPECT_EQ(check.ColumnString(0), "ok");
    }

    ASSERT_TRUE(db.Execute("ROLLBACK"));
  }
}

// A process killed mid-write, as a crash leaves it: nothing it was told is
// committed is lost, in a rollback journal or in a WAL, over 200 kills. The child
// is a real process: a fork can't run Chromium's base.
void KillLoop(const base::FilePath& root, const base::FilePath& path, bool wal) {
  const base::FilePath told = root.AppendASCII(wal ? "told-wal" : "told-journal");
  int committed = 0;

  for (int round = 0; round < 200; round++) {
    base::CommandLine command = base::GetMultiProcessTestChildBaseCommandLine();
    command.AppendSwitchPath("root", root);
    command.AppendSwitchPath("database", path);
    command.AppendSwitchPath("told", told);
    command.AppendSwitchASCII("first", base::NumberToString(committed));

    if (wal) {
      command.AppendSwitch("wal");
    }

    base::DeleteFile(told);
    base::Process child = base::SpawnMultiProcessTestChild(
        "PaneSqliteKilledWriter", command, base::LaunchOptions());
    ASSERT_TRUE(child.IsValid());

    // It has started writing; then a moment, different every round.
    for (int wait = 0; wait < 2000 && !base::PathExists(told); wait++) {
      usleep(1000);
    }

    usleep((round * 613) % 8000);
    kill(child.Pid(), SIGKILL);
    int code = 0;
    child.WaitForExit(&code);

    uint32_t last = committed;
    {
      base::File file(told, base::File::FLAG_OPEN | base::File::FLAG_READ);
      uint32_t value = 0;

      if (file.IsValid() &&
          file.Read(0, base::byte_span_from_ref(value)) == sizeof(value)) {
        last = value;
      }
    }

    sql::Database db(sql::DatabaseOptions().set_wal_mode(wal),
                     sql::test::kTestTag);
    ASSERT_TRUE(db.Open(path)) << "round " << round;

    if (db.DoesTableExist("t")) {
      sql::Statement count(db.GetUniqueStatement("SELECT COUNT(*) FROM t"));
      ASSERT_TRUE(count.Step());
      const int rows = count.ColumnInt(0);
      ASSERT_GE(rows, static_cast<int>(last)) << "round " << round;
      committed = rows;
      sql::Statement check(db.GetUniqueStatement("PRAGMA integrity_check"));
      ASSERT_TRUE(check.Step());
      ASSERT_EQ(check.ColumnString(0), "ok") << "round " << round;
    }
  }

  EXPECT_GT(committed, 0);
}

TEST_F(SqliteVfsTest, KilledWhileWritingAJournalLosesNothingCommitted) {
  KillLoop(root_, profile_.AppendASCII("History"), /*wal=*/false);
}

TEST_F(SqliteVfsTest, KilledWhileWritingAWalLosesNothingCommitted) {
  KillLoop(root_, profile_.AppendASCII("DIPS"), /*wal=*/true);
}

MULTIPROCESS_TEST_MAIN(PaneSqliteKilledWriter) {
  base::CommandLine* command = base::CommandLine::ForCurrentProcess();
  base::AtExitManager at_exit;
  AddRoots({command->GetSwitchValuePath("root")});
  Key key;
  key.fill(5);
  RegisterKey(kID, key);
  InstallSqliteVfs();

  sql::Database db(sql::DatabaseOptions().set_wal_mode(command->HasSwitch("wal")),
                   sql::test::kTestTag);

  if (!db.Open(command->GetSwitchValuePath("database")) ||
      !db.Execute("CREATE TABLE IF NOT EXISTS t(id INTEGER PRIMARY KEY, value TEXT)")) {
    return 2;
  }

  base::File told(command->GetSwitchValuePath("told"),
                  base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  int first = 0;
  base::StringToInt(command->GetSwitchValueASCII("first"), &first);

  for (int i = first;; i++) {
    sql::Statement insert(db.GetUniqueStatement("INSERT INTO t VALUES(?, ?)"));
    insert.BindInt(0, i);
    insert.BindString(1, std::string(600 + i % 3000, 'y'));

    if (!insert.Run()) {
      return 3;
    }

    // Told only once committed.
    const uint32_t done = i + 1;
    told.Write(0, base::byte_span_from_ref(done));
  }
}

}  // namespace
}  // namespace pane::encryption
