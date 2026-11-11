// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/proto_databases.h"

#include <memory>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "components/leveldb_proto/public/proto_database.h"
#include "components/leveldb_proto/public/proto_database_provider.h"
#include "components/leveldb_proto/public/shared_proto_database_client_list.h"
#include "components/leveldb_proto/testing/proto/test_db.pb.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"

namespace pane {
namespace {

// A profile's shared proto database, as a lock finds it in Chrome (ADR 0116):
// the profile goes while the database it opened as it started is still
// opening.
class SharedProtoDatabaseTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(profile_folder_.CreateUniqueTempDir()); }

  // The profile's provider, its shared database made for a client that asked,
  // as Chrome's in-progress downloads do as a profile starts, and its open
  // posted to the database's own sequence, which the thread pool runs only
  // when the test lets it (QUEUED).
  std::unique_ptr<leveldb_proto::ProtoDatabaseProvider> OpeningProvider() {
    auto provider = std::make_unique<leveldb_proto::ProtoDatabaseProvider>(
        profile_folder_.GetPath());
    std::unique_ptr<leveldb_proto::ProtoDatabase<leveldb_proto::TestProto>>
        client = provider->GetDB<leveldb_proto::TestProto>(
            leveldb_proto::ProtoDbType::TEST_DATABASE0,
            profile_folder_.GetPath().AppendASCII("TestDatabase0"),
            base::SequencedTaskRunner::GetCurrentDefault());
    client->Init(base::DoNothing());
    // The client's steps on this thread, up to the shared database's open.
    base::RunLoop().RunUntilIdle();
    // The profile's clients go with it.
    client.reset();
    return provider;
  }

  // Whether anything in this process still has the shared database open:
  // LevelDB takes a database's LOCK once per process.
  bool SharedDatabaseIsClosed() {
    std::unique_ptr<leveldb::DB> db;
    return leveldb_env::OpenDB(leveldb_env::Options(),
                               profile_folder_.GetPath()
                                   .AppendASCII("shared_proto_db")
                                   .AsUTF8Unsafe(),
                               &db)
        .ok();
  }

  base::test::TaskEnvironment environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED};
  base::ScopedTempDir profile_folder_;
};

// The cause, in Chrome as it is: the provider's Shutdown runs before the open
// ends, and the open's clean-up, posted two minutes on, keeps the database
// open past its profile. A lock's detach gave up after 10 s.
TEST_F(SharedProtoDatabaseTest,
       StaysOpenTwoMinutesWhenItsProfileGoesWhileItOpens) {
  std::unique_ptr<leveldb_proto::ProtoDatabaseProvider> provider =
      OpeningProvider();
  provider.reset();
  environment_.RunUntilIdle();

  EXPECT_FALSE(SharedDatabaseIsClosed());

  environment_.FastForwardBy(base::Seconds(119));
  EXPECT_FALSE(SharedDatabaseIsClosed());

  environment_.FastForwardBy(base::Seconds(1));
  EXPECT_TRUE(SharedDatabaseIsClosed());
}

TEST_F(SharedProtoDatabaseTest, LetGoClosesItAsItsOpenEnds) {
  std::unique_ptr<leveldb_proto::ProtoDatabaseProvider> provider =
      OpeningProvider();
  LetSharedProtoDatabaseGo(*provider);
  provider.reset();
  environment_.RunUntilIdle();

  EXPECT_TRUE(SharedDatabaseIsClosed());
}

// A database that opened while its profile was there closes with the
// profile, let go or not.
TEST_F(SharedProtoDatabaseTest, ClosesWithAProfileThatGoesAfterItOpened) {
  for (const bool let_go : {false, true}) {
    SCOPED_TRACE(let_go);
    std::unique_ptr<leveldb_proto::ProtoDatabaseProvider> provider =
        OpeningProvider();
    environment_.RunUntilIdle();
    ASSERT_FALSE(SharedDatabaseIsClosed());

    if (let_go) {
      LetSharedProtoDatabaseGo(*provider);
    }
    provider.reset();
    environment_.RunUntilIdle();

    EXPECT_TRUE(SharedDatabaseIsClosed());
  }
}

}  // namespace
}  // namespace pane
