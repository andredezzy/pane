// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/background_work.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/task/execution_fence.h"
#include "base/task/thread_pool.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "pane/encryption/engine_keys.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane {
namespace {

constexpr char kProfile[] = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02";

// What a task of the profile's found as it reached the profile's file.
enum Reached : int { kNotYet, kWithItsKey, kWithoutItsKey };

// Chrome's background work for a profile, as a lock finds it (ADR 0116):
// BEST_EFFORT tasks it posted as the profile was created and as it was
// destroyed, which reach the profile's files, held until Chrome's startup is
// over, or while the Mac is busy (Chromium's own fence holds them here, as
// it holds them for the startup).
class BackgroundWorkTest : public testing::Test {
 protected:
  void SetUp() override {
    // An absolute root on every OS; nothing is written under it.
    ASSERT_TRUE(data_.CreateUniqueTempDir());
    const base::FilePath engine = data_.GetPath().AppendASCII("Engine");
    file_ = engine.AppendASCII(kProfile).AppendASCII("DIPS");
    encryption::ResetForTesting();
    encryption::AddRoots({engine});
    encryption::Key key;
    key.fill(7);
    encryption::RegisterKey(kProfile, key);
  }

  void TearDown() override { encryption::ResetForTesting(); }

  // A task of the profile's, as the bounce-tracking database's open or the
  // bookmarks' ordering written: with its key it reaches the file; without,
  // the encryption ends the process (ADR 0121), which here it only tells.
  static void ReachTheProfilesFile(const base::FilePath& file,
                                   std::atomic<int>* reached) {
    if (encryption::KeyCount() == 0) {
      *reached = kWithoutItsKey;
      return;
    }
    *reached = encryption::KeysFor(file) ? kWithItsKey : kWithoutItsKey;
  }

  static void PostProfileWork(const base::FilePath& file,
                              std::atomic<int>* reached) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
        base::BindOnce(&ReachTheProfilesFile, file, reached));
  }

  base::test::TaskEnvironment environment_;
  base::ScopedTempDir data_;
  // A file of the profile's.
  base::FilePath file_;
};

// The cause, on Chromium's thread pool as it is: the lock's detach succeeded,
// since nothing was open yet, and the keys went; the held work then reached
// the profile's file without its key, where the encryption ended Pane.
TEST_F(BackgroundWorkTest, HeldWorkOutlivesTheKeysOfALockThatDoesNotWait) {
  std::atomic<int> reached = kNotYet;
  {
    base::ScopedBestEffortExecutionFence busy;
    PostProfileWork(file_, &reached);
    encryption::DropKeys();
  }
  environment_.RunUntilIdle();

  EXPECT_EQ(reached, kWithoutItsKey);
}

// The fix: the lock waits for the work before its detach, and drops the keys
// once it has run.
TEST_F(BackgroundWorkTest, WaitingLetsHeldWorkReachItsFileWithItsKey) {
  std::atomic<int> reached = kNotYet;
  std::optional<bool> ran;
  base::RunLoop waited;
  auto busy = std::make_unique<base::ScopedBestEffortExecutionFence>();
  PostProfileWork(file_, &reached);

  WaitForBackgroundWork(base::Seconds(30),
                        base::BindLambdaForTesting([&](bool work_ran) {
                          ran = work_ran;
                          encryption::DropKeys();
                          waited.Quit();
                        }));
  base::RunLoop().RunUntilIdle();
  // Still held: the lock still waits, the keys still there.
  EXPECT_FALSE(ran);
  EXPECT_EQ(reached, kNotYet);
  EXPECT_EQ(encryption::KeyCount(), 1u);

  busy.reset();
  waited.Run();

  EXPECT_EQ(ran, true);
  EXPECT_EQ(reached, kWithItsKey);
  EXPECT_EQ(encryption::KeyCount(), 0u);
}

// What that work posts in turn is waited for too: a marker posted after it
// would have answered once it had begun.
TEST_F(BackgroundWorkTest, WhatTheWorkPostsInTurnIsWaitedFor) {
  std::atomic<int> reached = kNotYet;
  std::optional<bool> ran;
  base::RunLoop waited;
  auto busy = std::make_unique<base::ScopedBestEffortExecutionFence>();
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&PostProfileWork, file_, &reached));

  WaitForBackgroundWork(base::Seconds(30),
                        base::BindLambdaForTesting([&](bool work_ran) {
                          ran = work_ran;
                          EXPECT_EQ(reached, kWithItsKey);
                          waited.Quit();
                        }));
  busy.reset();
  waited.Run();

  EXPECT_EQ(ran, true);
}

// Held past the cap, the lock goes on without it.
TEST_F(BackgroundWorkTest, TheCapEndsTheWait) {
  std::atomic<int> reached = kNotYet;
  std::optional<bool> ran;
  base::RunLoop waited;
  auto busy = std::make_unique<base::ScopedBestEffortExecutionFence>();
  PostProfileWork(file_, &reached);

  WaitForBackgroundWork(base::Milliseconds(50),
                        base::BindLambdaForTesting([&](bool work_ran) {
                          ran = work_ran;
                          waited.Quit();
                        }));
  waited.Run();

  EXPECT_EQ(ran, false);
  EXPECT_EQ(reached, kNotYet);

  busy.reset();
  environment_.RunUntilIdle();
  EXPECT_EQ(reached, kWithItsKey);
}

// Nothing held: the answer comes at once.
TEST_F(BackgroundWorkTest, AnIdlePoolAnswersAtOnce) {
  std::optional<bool> ran;
  base::RunLoop waited;
  WaitForBackgroundWork(base::Seconds(30),
                        base::BindLambdaForTesting([&](bool work_ran) {
                          ran = work_ran;
                          waited.Quit();
                        }));
  waited.Run();

  EXPECT_EQ(ran, true);
}

}  // namespace
}  // namespace pane
