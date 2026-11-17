// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/handover.h"

#include <atomic>
#include <optional>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/thread_pool.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "pane/encryption/engine_keys.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::encryption {
namespace {

constexpr char kProfile[] = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02";

// What a write on the pool found as it reached the profile's file.
enum Reached : int { kNotYet, kWithItsKey, kWithoutItsKey };

// The network service's drop at a lock (W9a, Windows' test VM): a closed
// profile's last state, written by an ImportantFileWriter on the service's
// own thread pool, still under way as the lock's drop came, met the
// encryption's check of a profile's file with no key, which ended the
// service (3 in 16 Locks over a streaming HTTPS video). The write is held
// here until the test lets it go, as the VM's load held it.
class DropKeysAtLockTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(data_.CreateUniqueTempDir());
    const base::FilePath engine = data_.GetPath().AppendASCII("Engine");
    file_ = engine.AppendASCII(kProfile).AppendASCII("TransportSecurity");
    ResetForTesting();
    AddRoots({engine});
    Register();
  }

  void TearDown() override {
    // Nothing held is left blocked past the test.
    write_may_go_.Signal();
    environment_.RunUntilIdle();
    ResetForTesting();
  }

  static void Register() {
    Key key;
    key.fill(7);
    RegisterKey(kProfile, key);
  }

  // The write, on the pool, as an ImportantFileWriter's: it waits to be let
  // go, then reaches the profile's file. Without its key the encryption
  // would end the process (ADR 0121); here it only tells.
  void PostTheLastWrite() {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(
            [](base::WaitableEvent* may_go, const base::FilePath& file,
               std::atomic<int>* reached) {
              base::ScopedAllowBaseSyncPrimitivesForTesting wait;
              may_go->Wait();
              if (KeyCount() == 0) {
                *reached = kWithoutItsKey;
                return;
              }
              *reached = KeysFor(file) ? kWithItsKey : kWithoutItsKey;
            },
            &write_may_go_, file_, &reached_));
  }

  // The lock's drop, asked now, as the services ask it; its answer kept.
  void AskToDrop(base::TimeDelta cap = base::Seconds(30)) {
    DropKeysOnceBackgroundWorkRan(
        cap, base::BindLambdaForTesting([this](uint32_t left) {
          answer_ = left;
          answered_.Quit();
        }));
  }

  // The main thread's tasks alone: the pool's held write stays held.
  static void RunMainThread() { base::RunLoop().RunUntilIdle(); }

  base::test::TaskEnvironment environment_;
  base::ScopedTempDir data_;
  base::FilePath file_;
  base::WaitableEvent write_may_go_;
  std::atomic<int> reached_ = kNotYet;
  std::optional<uint32_t> answer_;
  base::RunLoop answered_;
};

// The fix: the drop waits for the write, which reaches its file with its key,
// and the keys go after it.
TEST_F(DropKeysAtLockTest, TheDropWaitsForTheLastWrite) {
  PostTheLastWrite();
  AskToDrop();
  RunMainThread();

  EXPECT_FALSE(answer_);
  EXPECT_EQ(KeyCount(), 1u);

  write_may_go_.Signal();
  answered_.Run();

  EXPECT_EQ(reached_, kWithItsKey);
  EXPECT_EQ(answer_, 0u);
  EXPECT_EQ(KeyCount(), 0u);
}

// The race the wait opens: an unlock's network context registers the keys
// while the drop waits. The drop then leaves them, and the profiles the unlock
// opens find them.
TEST_F(DropKeysAtLockTest, KeysAnUnlockRegisteredMeanwhileStay) {
  PostTheLastWrite();
  AskToDrop();
  RunMainThread();
  Register();

  write_may_go_.Signal();
  answered_.Run();

  EXPECT_EQ(reached_, kWithItsKey);
  EXPECT_EQ(answer_, 1u);
  EXPECT_EQ(KeyCount(), 1u);
  EXPECT_TRUE(KeysFor(file_));
}

// A registration before the drop was asked is the lock's own to drop.
TEST_F(DropKeysAtLockTest, KeysRegisteredBeforeTheAskGo) {
  Register();
  AskToDrop();
  answered_.Run();

  EXPECT_EQ(answer_, 0u);
  EXPECT_EQ(KeyCount(), 0u);
}

// Held past the cap, the keys go all the same: a write that comes later meets
// the encryption's check, which ends the process, as before the wait.
TEST_F(DropKeysAtLockTest, TheCapDropsTheKeys) {
  PostTheLastWrite();
  AskToDrop(base::Milliseconds(50));
  answered_.Run();

  EXPECT_EQ(answer_, 0u);
  EXPECT_EQ(KeyCount(), 0u);

  write_may_go_.Signal();
  environment_.RunUntilIdle();
  EXPECT_EQ(reached_, kWithoutItsKey);
}

// Nothing on the pool: the answer comes at once.
TEST_F(DropKeysAtLockTest, AnIdlePoolDropsAtOnce) {
  AskToDrop();
  answered_.Run();

  EXPECT_EQ(answer_, 0u);
  EXPECT_EQ(KeyCount(), 0u);
}

}  // namespace
}  // namespace pane::encryption
