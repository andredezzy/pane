// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/data_lock.h"

#include <memory>

#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's DataLockTests, in its DataVolumeTests.

// The data's lock is one Pane's while it runs, and the next one's once it
// lets go.
TEST(DataLockTest, Held) {
  base::ScopedTempDir root;
  ASSERT_TRUE(root.CreateUniqueTempDir());

  std::unique_ptr<DataLock> first = DataLock::Take(root.GetPath());
  EXPECT_TRUE(first);
  EXPECT_FALSE(DataLock::Take(root.GetPath()));

  // Taken as the next Pane takes it (main.swift), waiting: a process another
  // test starts holds the parent's descriptors until it execs, which under
  // the thread sanitizer outlasted the release (CI).
  first.reset();
  EXPECT_TRUE(DataLock::Wait(root.GetPath(), base::Seconds(3)));
}

// A Pane started while another quits takes the lock once that one lets go.
TEST(DataLockTest, Waits) {
  base::ScopedTempDir root;
  ASSERT_TRUE(root.CreateUniqueTempDir());

  // The wait can take the lock only once the first lets go, so no clock is
  // checked: a sanitized build on a loaded CI runner took 4.4 s.
  std::unique_ptr<DataLock> first = DataLock::Take(root.GetPath());
  std::unique_ptr<DataLock> taken;
  base::WaitableEvent waited;
  base::Thread waiting("DataLockWait");
  ASSERT_TRUE(waiting.Start());
  waiting.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](const base::FilePath& root, std::unique_ptr<DataLock>* taken,
             base::WaitableEvent* waited) {
            *taken = DataLock::Wait(root, base::Seconds(30));
            waited->Signal();
          },
          root.GetPath(), base::Unretained(&taken), base::Unretained(&waited)));

  base::PlatformThread::Sleep(base::Milliseconds(200));
  EXPECT_TRUE(first);
  first.reset();

  waited.Wait();
  EXPECT_TRUE(taken);
  EXPECT_FALSE(DataLock::Wait(root.GetPath(), base::Milliseconds(100)));
}

// A process Pane starts does not keep the lock: the update's relaunch takes
// it.
TEST(DataLockTest, NotInherited) {
  base::ScopedTempDir root;
  ASSERT_TRUE(root.CreateUniqueTempDir());

  std::unique_ptr<DataLock> lock = DataLock::Take(root.GetPath());
  base::Process child =
      base::LaunchProcess({"/bin/sleep", "5"}, base::LaunchOptions());
  ASSERT_TRUE(child.IsValid());

  // Waited for briefly, as in Held: the child sleeps for longer, so a lock it
  // inherited would still fail this.
  EXPECT_TRUE(lock);
  lock.reset();
  EXPECT_TRUE(DataLock::Wait(root.GetPath(), base::Seconds(2)));

  EXPECT_TRUE(child.Terminate(0, /*wait=*/true));
}

}  // namespace
}  // namespace pane::model
