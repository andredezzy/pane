// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/data_vault.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "pane/model/data_key.h"
#include "pane/model/mac/data_lock.h"
#include "pane/model/mac/data_volume.h"
#include "pane/model/mac/file_operations.h"
#include "pane/model/mac/vault_test_support.h"
#include "pane/model/profile_deletions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// The vault's rules, in the order its work is posted in (ADR 0113, 0116,
// 0121): the Swift app's DataVault had no unit tests, its scenarios ran them
// on a real volume. These run it on disk image tools and an engine conversion
// in memory, and on the volume's folders as they are.

using Phase = DataVault::Phase;
using Gate = FakeFileMigrator::Gate;

bool Contains(const std::vector<std::string>& list, std::string_view item) {
  return std::ranges::find(list, item) != list.end();
}

Key TestKey() {
  Key key;
  key.fill(7);
  return key;
}

// What the vault tells the app.
class Delegate : public DataVault::Delegate {
 public:
  void DataKeyDidOpen(base::OnceClosure done) override {
    events.push_back("key");
    if (hold_loads) {
      loaded = std::move(done);
      return;
    }
    std::move(done).Run();
  }
  std::vector<std::string> ProfileIds() override { return ids; }
  void AccessDidOpen() override { events.push_back("access"); }
  void VolumeWontOpen() override { events.push_back("wont-open"); }
  void QuitToCloseVolume() override { events.push_back("quit"); }
  void VolumeDidDetachAtLock() override { events.push_back("detached"); }

  std::vector<std::string> ids;
  std::vector<std::string> events;
  // The profiles load off the window's sequence: held here until the test
  // says they have.
  bool hold_loads = false;
  base::OnceClosure loaded;
};

class DataVaultTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    data_ = directory_.GetPath().Append("data");
    ASSERT_TRUE(base::CreateDirectory(data_.Append("Engine")));
    tools_ = base::MakeRefCounted<FakeVolumeTools>(Volume());
    migrator_ = base::MakeRefCounted<FakeFileMigrator>();
  }

  // Nothing of the test's own holds a worker past its end.
  void TearDown() override {
    opens_.Signal();
    tools_->HoldOpens(nullptr);
    migrator_->ReleaseAll();
    task_environment_.RunUntilIdle();
  }

  DataVolume Volume() const { return DataVolume(data_, std::nullopt); }

  // The vault, as the app makes it once the thread pool runs: with the data's
  // lock, taken at launch, unless another Pane held it then.
  std::unique_ptr<DataVault> MakeVault(
      scoped_refptr<LaunchAttach> launch = nullptr,
      bool holds_data_lock = true) {
    DataVault::DetachWaits waits;
    waits.at_lock = {base::TimeDelta(), base::Milliseconds(1),
                     base::Milliseconds(1)};
    waits.at_exit = {base::TimeDelta(), base::Milliseconds(1)};
    auto vault = std::make_unique<DataVault>(
        data_, std::nullopt, tools_,
        holds_data_lock ? DataLock::Take(data_) : nullptr, std::move(launch),
        &delegate_, std::move(waits));
    vault->set_file_migrator(migrator_->Get());
    return vault;
  }

  // As AppLock opens it: the attach starts where the key opened, then the
  // vault takes the key.
  static void Open(DataVault& vault) {
    vault.MakeAttacher().Run(TestKey());
    vault.Open(TestKey());
  }

  // A profile's folder in the volume, as an earlier open left it.
  void MakeProfileFolder(std::string_view id) {
    ASSERT_TRUE(base::CreateDirectory(Volume().Folder(id).Append("Profile")));
  }

  base::FilePath Marker(std::string_view id, std::string_view name) const {
    return Volume().Folder(id).Append(name);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  // What a held open waits for (FakeVolumeTools::HoldOpens).
  base::WaitableEvent opens_;
  base::ScopedTempDir directory_;
  base::FilePath data_;
  scoped_refptr<FakeVolumeTools> tools_;
  scoped_refptr<FakeFileMigrator> migrator_;
  Delegate delegate_;
};

// The key opens the profiles, and the volume attaches behind them: a profile
// waiting for the attach opens once it is mounted.
TEST_F(DataVaultTest, OpensOnceAttached) {
  std::unique_ptr<DataVault> vault = MakeVault();
  base::test::TestFuture<bool> attached;
  vault->WhenAttached(attached.GetCallback());
  EXPECT_EQ(vault->phase(), Phase::kClosed);

  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return attached.IsReady(); }));
  EXPECT_TRUE(attached.Get());
  EXPECT_TRUE(vault->is_attached());
  EXPECT_EQ(vault->phase(), Phase::kOpen);
  EXPECT_EQ(tools_->calls(),
            (std::vector<std::string>{"attach-or-make", "watch", "open"}));
  EXPECT_EQ(delegate_.events, (std::vector<std::string>{"key", "access"}));
  EXPECT_TRUE(vault->attached_at());
  EXPECT_FALSE(vault->failure());
  EXPECT_EQ(vault->FilesKey(), FilesKey(TestKey()));
  EXPECT_EQ(vault->EngineKey("alpha"), EngineFilesKey(TestKey(), "alpha"));

  // Asked again, it answers at once.
  bool again = false;
  vault->WhenAttached(
      base::BindLambdaForTesting([&](bool yes) { again = yes; }));
  EXPECT_TRUE(again);
}

// The profiles load off the window's sequence: the vault goes on, with the
// attach's outcome, the plain folders, and the profiles' own work, only once
// they have.
TEST_F(DataVaultTest, GoesOnOnceTheProfilesLoad) {
  delegate_.ids = {"alpha"};
  delegate_.hold_loads = true;
  std::unique_ptr<DataVault> vault = MakeVault();
  base::test::TestFuture<bool> attached;
  vault->WhenAttached(attached.GetCallback());

  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return tools_->calls().size() == 3u; }));
  RunFor(base::Milliseconds(50));
  EXPECT_FALSE(attached.IsReady());
  EXPECT_FALSE(vault->is_attached());
  EXPECT_EQ(vault->phase(), Phase::kClosed);
  EXPECT_EQ(delegate_.events, (std::vector<std::string>{"key"}));

  std::move(delegate_.loaded).Run();
  ASSERT_TRUE(WaitFor([&] { return attached.IsReady(); }));
  EXPECT_TRUE(attached.Get());
  EXPECT_EQ(vault->phase(), Phase::kOpen);
  EXPECT_EQ(delegate_.events, (std::vector<std::string>{"key", "access"}));
}

// A lock before the profiles have loaded: nothing of that open goes on once
// they do, and the image its key attached detaches behind the lock.
TEST_F(DataVaultTest, ALockBeforeTheProfilesLoad) {
  delegate_.ids = {"alpha"};
  delegate_.hold_loads = true;
  std::unique_ptr<DataVault> vault = MakeVault();
  base::test::TestFuture<bool> attached;
  vault->WhenAttached(attached.GetCallback());

  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return tools_->calls().size() == 3u; }));
  base::OnceCallback<void(bool)> closed = vault->Lock();
  EXPECT_FALSE(attached.Get());

  std::move(delegate_.loaded).Run();
  std::move(closed).Run(true);
  ASSERT_TRUE(WaitFor([&] { return Contains(delegate_.events, "detached"); }));
  EXPECT_EQ(vault->phase(), Phase::kClosed);
  EXPECT_FALSE(vault->is_attached());
  EXPECT_FALSE(Contains(delegate_.events, "access"));
  EXPECT_FALSE(vault->FilesKey());
}

// The image the launch attached, its volume locked, is what the key unlocks:
// no second attach, and one watchdog for the Pane's life.
TEST_F(DataVaultTest, UnlocksTheImageTheLaunchAttached) {
  ASSERT_TRUE(base::CreateDirectory(Volume().image()));
  scoped_refptr<LaunchAttach> launch =
      LaunchAttach::Start(Volume(), tools_, DataVault::WatchdogPath());
  ASSERT_TRUE(launch->Wait(base::TimeDelta::Max()));
  ASSERT_TRUE(launch->devices());

  std::unique_ptr<DataVault> vault = MakeVault(launch);
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));
  EXPECT_EQ(tools_->calls(),
            (std::vector<std::string>{"attach-image", "watch", "open"}));
}

// A fresh install has no image until the key first opens: the launch
// attaches nothing, and the key's open makes it.
TEST_F(DataVaultTest, NoImageAttachesAtLaunch) {
  scoped_refptr<LaunchAttach> launch =
      LaunchAttach::Start(Volume(), tools_, DataVault::WatchdogPath());
  ASSERT_TRUE(launch->Wait(base::TimeDelta::Max()));
  EXPECT_FALSE(launch->devices());
  EXPECT_TRUE(tools_->calls().empty());

  std::unique_ptr<DataVault> vault = MakeVault(launch);
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));
  EXPECT_EQ(tools_->calls(),
            (std::vector<std::string>{"attach-or-make", "watch", "open"}));
}

// An earlier Pane's plain folders move in before any profile opens, behind
// the panel, and their plain copies are erased after.
TEST_F(DataVaultTest, MovesAnEarlierPanesFoldersInFirst) {
  delegate_.ids = {"alpha"};
  const base::FilePath plain = data_.Append("Engine").Append("alpha");
  ASSERT_TRUE(base::CreateDirectory(plain));
  ASSERT_TRUE(base::WriteFile(plain.Append("Cookies"), "cookies"));

  std::unique_ptr<DataVault> vault = MakeVault();
  vault->ExpectMove();
  EXPECT_EQ(vault->phase(), Phase::kMoving);
  EXPECT_EQ(vault->share(), 0);

  bool moved_in = false;
  vault->WhenAttached(base::BindLambdaForTesting([&](bool attached) {
    base::FilePath target;
    moved_in =
        attached &&
        base::PathExists(
            Volume().Folder("alpha").Append("Profile").Append("Cookies")) &&
        base::ReadSymbolicLink(plain, &target);
  }));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));
  EXPECT_TRUE(moved_in);
  EXPECT_EQ(vault->phase(), Phase::kOpen);
  EXPECT_EQ(delegate_.events, (std::vector<std::string>{"key", "access"}));
  EXPECT_TRUE(
      WaitFor([&] { return !base::PathExists(data_.Append("Wiping")); }));
}

// The profiles deleted while the volume was detached go once it attaches, and
// an id that is no folder's name removes nothing.
TEST_F(DataVaultTest, PurgesDeletedProfilesOnceAttached) {
  MakeProfileFolder("gone");
  const base::FilePath link = data_.Append("Engine").Append("gone");
  ASSERT_TRUE(base::CreateSymbolicLink(
      Volume().Folder("gone").Append("Profile"), link));
  ProfileDeletions(data_).Add("gone");
  ProfileDeletions(data_).Add("..");

  std::unique_ptr<DataVault> vault = MakeVault();
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));
  EXPECT_FALSE(base::PathExists(Volume().Folder("gone")));
  EXPECT_FALSE(ItemExists(link));
  EXPECT_TRUE(ProfileDeletions(data_).Pending().empty());
  EXPECT_TRUE(base::PathExists(data_.Append("Engine")));
  EXPECT_TRUE(base::PathExists(Volume().mount_point()));
}

// A lock's detach comes after the attach still under way (ADR 0116): it is
// the attach's device it detaches, and a profile waiting for the attach opens
// no more.
TEST_F(DataVaultTest, DetachesAfterTheAttachUnderWay) {
  tools_->HoldOpens(&opens_);
  std::unique_ptr<DataVault> vault = MakeVault();
  base::test::TestFuture<bool> attached;
  vault->WhenAttached(attached.GetCallback());
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return tools_->Count("open") == 1; }));

  base::OnceCallback<void(bool)> closed = vault->Lock();
  ASSERT_TRUE(attached.IsReady());
  EXPECT_FALSE(attached.Get());
  EXPECT_EQ(vault->phase(), Phase::kClosed);
  EXPECT_FALSE(vault->data_key());
  std::move(closed).Run(true);
  RunFor(base::Milliseconds(50));
  EXPECT_EQ(tools_->Count("detach"), 0);

  opens_.Signal();
  ASSERT_TRUE(WaitFor([&] { return Contains(delegate_.events, "detached"); }));
  EXPECT_EQ(tools_->calls(), (std::vector<std::string>{
                                 "attach-or-make", "watch", "open", "detach"}));
  EXPECT_FALSE(tools_->attached());
  EXPECT_FALSE(vault->is_attached());
}

// The PIN typed while a profile still closes waits for the lock's detach
// before it attaches again; the watchdog started once serves both.
TEST_F(DataVaultTest, ThePinWaitsForTheLocksDetach) {
  std::unique_ptr<DataVault> vault = MakeVault();
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));

  base::OnceCallback<void(bool)> closed = vault->Lock();
  base::test::TestFuture<bool> attached;
  vault->WhenAttached(attached.GetCallback());
  Open(*vault);
  RunFor(base::Milliseconds(50));
  EXPECT_EQ(tools_->calls(),
            (std::vector<std::string>{"attach-or-make", "watch", "open"}));
  EXPECT_FALSE(attached.IsReady());

  std::move(closed).Run(true);
  ASSERT_TRUE(WaitFor([&] { return attached.IsReady(); }));
  EXPECT_TRUE(attached.Get());
  EXPECT_EQ(tools_->calls(),
            (std::vector<std::string>{"attach-or-make", "watch", "open",
                                      "detach", "attach-or-make", "open"}));
  EXPECT_TRUE(Contains(delegate_.events, "detached"));
}

// With the engine running a lock never forces the detach, which would pull
// the files from under it (SIGBUS): past its 10 s Pane quits, and the one
// forced detach comes once the engine has stopped.
TEST_F(DataVaultTest, NeverForcesADetachAtALock) {
  std::unique_ptr<DataVault> vault = MakeVault();
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));
  tools_->FailDetaches(1000);

  base::OnceCallback<void(bool)> closed = vault->Lock();
  std::move(closed).Run(true);
  ASSERT_TRUE(WaitFor([&] { return Contains(delegate_.events, "quit"); }));
  EXPECT_EQ(tools_->Count("detach"), 3);
  EXPECT_EQ(tools_->Count("force-detach"), 0);
  EXPECT_FALSE(Contains(delegate_.events, "detached"));
  EXPECT_TRUE(tools_->attached());

  vault->EngineDidStop();
  EXPECT_EQ(tools_->Count("force-detach"), 1);
  EXPECT_FALSE(tools_->attached());
  // The image is off, so its watchdog ends.
  EXPECT_TRUE(tools_->WatchdogEnded(base::Seconds(10)));
}

// A profile that would not close keeps its files open: the image stays
// attached, untouched, for the quit to detach with the engine stopped.
TEST_F(DataVaultTest, AProfileStillOpenLeavesTheImageForTheQuit) {
  std::unique_ptr<DataVault> vault = MakeVault();
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));

  base::OnceCallback<void(bool)> closed = vault->Lock();
  std::move(closed).Run(false);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(tools_->Count("detach"), 0);
  EXPECT_TRUE(tools_->attached());

  vault->EngineDidStop();
  EXPECT_EQ(tools_->calls().back(), "detach");
  EXPECT_FALSE(tools_->attached());
}

// A lock while a profile converts: the detach waits for the conversion, which
// has the volume's files open, and nothing of that open goes on after it.
TEST_F(DataVaultTest, ALockWhileAProfileConverts) {
  delegate_.ids = {"alpha"};
  MakeProfileFolder("alpha");
  scoped_refptr<Gate> converting = migrator_->Hold("migrate", "alpha");
  std::unique_ptr<DataVault> vault = MakeVault();
  base::test::TestFuture<bool> prepared;
  vault->WhenAttached(base::BindLambdaForTesting([&](bool attached) {
    ASSERT_TRUE(attached);
    vault->Prepare("alpha", true, prepared.GetCallback());
  }));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] {
    return migrator_->calls() == std::vector<std::string>{"migrate alpha"};
  }));

  base::OnceCallback<void(bool)> closed = vault->Lock();
  std::move(closed).Run(true);
  RunFor(base::Milliseconds(50));
  EXPECT_EQ(tools_->Count("detach"), 0);

  converting->Release();
  ASSERT_TRUE(WaitFor([&] { return Contains(delegate_.events, "detached"); }));
  ASSERT_TRUE(WaitFor([&] { return prepared.IsReady(); }));
  // The open it was for is over.
  EXPECT_FALSE(prepared.Get());
  EXPECT_EQ(migrator_->calls(), std::vector<std::string>{"migrate alpha"});
  EXPECT_EQ(tools_->calls().back(), "detach");
}

// A restore and a conversion never share a folder: after two opens in a row a
// kill ended, the safety copy comes back first, then the files convert again.
TEST_F(DataVaultTest, ARestoreAndAConversionNeverTogether) {
  delegate_.ids = {"alpha"};
  MakeProfileFolder("alpha");
  ASSERT_TRUE(base::WriteFile(Marker("alpha", "SafetyCopy"), "copy"));
  ASSERT_TRUE(Touch(Marker("alpha", ".safety-started")));
  ASSERT_TRUE(Touch(Marker("alpha", ".safety-killed")));
  scoped_refptr<Gate> restoring = migrator_->Hold("restore", "alpha");

  std::unique_ptr<DataVault> vault = MakeVault();
  base::test::TestFuture<bool> prepared;
  vault->WhenAttached(base::BindLambdaForTesting([&](bool attached) {
    vault->Prepare("alpha", true, prepared.GetCallback());
  }));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] {
    return migrator_->calls() == std::vector<std::string>{"restore alpha"};
  }));
  RunFor(base::Milliseconds(50));
  EXPECT_EQ(migrator_->calls(), std::vector<std::string>{"restore alpha"});

  restoring->Release();
  ASSERT_TRUE(WaitFor([&] { return prepared.IsReady(); }));
  EXPECT_TRUE(prepared.Get());
  EXPECT_EQ(migrator_->calls(),
            (std::vector<std::string>{"restore alpha", "migrate alpha"}));
  EXPECT_EQ(migrator_->most_at_once(), 1);
  EXPECT_TRUE(Volume().FilesAreEncrypted("alpha"));
  EXPECT_TRUE(base::PathExists(Marker("alpha", ".safety-restored")));
}

// The profile a window waits for goes at once; the others convert behind it,
// one at a time.
TEST_F(DataVaultTest, TheWindowsProfileFirst) {
  delegate_.ids = {"alpha", "beta"};
  MakeProfileFolder("alpha");
  MakeProfileFolder("beta");
  scoped_refptr<Gate> behind = migrator_->Hold("migrate", "alpha");

  std::unique_ptr<DataVault> vault = MakeVault();
  base::test::TestFuture<bool> prepared;
  vault->WhenAttached(base::BindLambdaForTesting([&](bool attached) {
    vault->Prepare("beta", true, prepared.GetCallback());
  }));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return prepared.IsReady(); }));
  EXPECT_TRUE(prepared.Get());
  EXPECT_TRUE(Volume().FilesAreEncrypted("beta"));
  EXPECT_FALSE(Volume().FilesAreEncrypted("alpha"));

  behind->Release();
  ASSERT_TRUE(WaitFor([&] { return Volume().FilesAreEncrypted("alpha"); }));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(migrator_->Count("migrate alpha"), 1);
  EXPECT_EQ(migrator_->Count("migrate beta"), 1);
  EXPECT_EQ(migrator_->most_at_once(), 1);
}

// The conversion a window waits for shows on the panel past a second, with
// how far it is (Paper 07.5), and the window opens once it ends.
TEST_F(DataVaultTest, ShowsAConversionPastASecond) {
  delegate_.ids = {"alpha"};
  MakeProfileFolder("alpha");
  scoped_refptr<Gate> converting = migrator_->Hold("migrate", "alpha");
  std::unique_ptr<DataVault> vault = MakeVault();
  base::test::TestFuture<bool> prepared;
  vault->WhenAttached(base::BindLambdaForTesting([&](bool attached) {
    vault->Prepare("alpha", true, prepared.GetCallback());
  }));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] {
    return migrator_->calls() == std::vector<std::string>{"migrate alpha"};
  }));
  RunFor(base::Milliseconds(20));
  EXPECT_EQ(vault->phase(), Phase::kOpen);

  task_environment_.AdvanceClock(base::Milliseconds(999));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(vault->phase(), Phase::kOpen);
  task_environment_.AdvanceClock(base::Milliseconds(1));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(vault->phase(), Phase::kMoving);
  EXPECT_EQ(vault->share(), 0.5);

  converting->Release();
  ASSERT_TRUE(WaitFor([&] { return prepared.IsReady(); }));
  EXPECT_TRUE(prepared.Get());
  ASSERT_TRUE(WaitFor([&] { return vault->phase() == Phase::kOpen; }));
  EXPECT_EQ(delegate_.events.back(), "access");
}

// A profile up for 5 s counts the open for its safety copy (ADR 0121).
TEST_F(DataVaultTest, CountsAnOpenPastFiveSeconds) {
  delegate_.ids = {"alpha"};
  MakeProfileFolder("alpha");
  ASSERT_TRUE(base::WriteFile(Marker("alpha", "SafetyCopy"), "copy"));
  Volume().MarkFilesEncrypted("alpha");
  std::unique_ptr<DataVault> vault = MakeVault();
  base::test::TestFuture<bool> prepared;
  vault->WhenAttached(base::BindLambdaForTesting([&](bool attached) {
    vault->Prepare("alpha", true, prepared.GetCallback());
  }));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return prepared.IsReady(); }));
  ASSERT_TRUE(prepared.Get());
  EXPECT_TRUE(base::PathExists(Marker("alpha", ".safety-started")));

  vault->ProfileDidOpen("alpha");
  task_environment_.AdvanceClock(base::Milliseconds(4999));
  RunFor(base::Milliseconds(20));
  EXPECT_FALSE(base::PathExists(Marker("alpha", ".safety-opened-1")));
  task_environment_.AdvanceClock(base::Milliseconds(1));
  ASSERT_TRUE(WaitFor(
      [&] { return base::PathExists(Marker("alpha", ".safety-opened-1")); }));
  EXPECT_FALSE(base::PathExists(Marker("alpha", ".safety-started")));
}

// A lock inside those 5 s closed the open: its timer counts nothing, and the
// detach marks the open closed, not killed.
TEST_F(DataVaultTest, ALockWithinFiveSecondsCountsNothing) {
  delegate_.ids = {"alpha"};
  MakeProfileFolder("alpha");
  ASSERT_TRUE(base::WriteFile(Marker("alpha", "SafetyCopy"), "copy"));
  Volume().MarkFilesEncrypted("alpha");
  std::unique_ptr<DataVault> vault = MakeVault();
  base::test::TestFuture<bool> prepared;
  vault->WhenAttached(base::BindLambdaForTesting([&](bool attached) {
    vault->Prepare("alpha", true, prepared.GetCallback());
  }));
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return prepared.IsReady(); }));
  vault->ProfileDidOpen("alpha");

  base::OnceCallback<void(bool)> closed = vault->Lock();
  std::move(closed).Run(true);
  ASSERT_TRUE(WaitFor([&] { return Contains(delegate_.events, "detached"); }));
  EXPECT_FALSE(base::PathExists(Marker("alpha", ".safety-started")));
  task_environment_.AdvanceClock(base::Seconds(5));
  RunFor(base::Milliseconds(20));
  EXPECT_FALSE(base::PathExists(Marker("alpha", ".safety-opened-1")));
}

// A volume that won't open says so (Paper 07.8): no profile opens, and the
// panel gives way to the lock's line.
TEST_F(DataVaultTest, AVolumeThatWontOpenSaysSo) {
  tools_->FailOpens(VolumeError::NotEncrypted("disk90s1"));
  std::unique_ptr<DataVault> vault = MakeVault();
  vault->ExpectMove();
  base::test::TestFuture<bool> attached;
  vault->WhenAttached(attached.GetCallback());
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return attached.IsReady(); }));
  EXPECT_FALSE(attached.Get());
  EXPECT_TRUE(Contains(delegate_.events, "wont-open"));
  EXPECT_TRUE(vault->failure());
  EXPECT_FALSE(vault->is_attached());
  EXPECT_EQ(vault->phase(), Phase::kOpen);

  bool answered = true;
  vault->WhenAttached(
      base::BindLambdaForTesting([&](bool yes) { answered = yes; }));
  EXPECT_FALSE(answered);
}

// A Pane started while another held the data attaches only once that one has
// let go of its lock, and holds it from then.
TEST_F(DataVaultTest, WaitsForTheDataLockAnotherPaneHeld) {
  std::unique_ptr<DataLock> other = DataLock::Take(data_);
  ASSERT_TRUE(other);
  std::unique_ptr<DataVault> vault =
      MakeVault(nullptr, /*holds_data_lock=*/false);
  Open(*vault);
  RunFor(base::Milliseconds(200));
  EXPECT_TRUE(tools_->calls().empty());

  other.reset();
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));
  EXPECT_EQ(tools_->calls().front(), "attach-or-make");
  EXPECT_FALSE(DataLock::Take(data_));
}

// The quit waits for an attach still under way (the first launch's making of
// the image, say: a quit in it had left the image attached, after Pane and
// before its watchdog), then detaches what it attached (ADR 0113).
TEST_F(DataVaultTest, TheQuitWaitsForTheAttachUnderWay) {
  tools_->HoldOpens(&opens_);
  std::unique_ptr<DataVault> vault = MakeVault();
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return tools_->Count("open") == 1; }));

  // The attach ends a moment into the quit.
  base::Thread attach_ends("attach ends");
  ASSERT_TRUE(attach_ends.Start());
  attach_ends.task_runner()->PostTask(
      FROM_HERE, base::BindLambdaForTesting([&] {
        base::PlatformThread::Sleep(base::Milliseconds(100));
        opens_.Signal();
      }));
  vault->EngineDidStop();
  EXPECT_EQ(tools_->calls(), (std::vector<std::string>{
                                 "attach-or-make", "watch", "open", "detach"}));
  EXPECT_FALSE(tools_->attached());
  EXPECT_TRUE(tools_->WatchdogEnded(base::Seconds(10)));
}

// At the quit, the image the launch attached is detached, the key never
// opened, and its watchdog ends.
TEST_F(DataVaultTest, TheQuitDetachesTheLaunchImage) {
  ASSERT_TRUE(base::CreateDirectory(Volume().image()));
  scoped_refptr<LaunchAttach> launch =
      LaunchAttach::Start(Volume(), tools_, DataVault::WatchdogPath());
  ASSERT_TRUE(launch->Wait(base::TimeDelta::Max()));
  std::unique_ptr<DataVault> vault = MakeVault(launch);

  vault->EngineDidStop();
  EXPECT_EQ(tools_->calls(),
            (std::vector<std::string>{"attach-image", "watch", "detach"}));
  EXPECT_FALSE(tools_->attached());
  EXPECT_TRUE(tools_->WatchdogEnded(base::Seconds(10)));
}

// A device remembered from the launch is detached only while it still holds
// Pane's image (0.19.1): its number goes to the next disk attached.
TEST_F(DataVaultTest, TheQuitLeavesADeviceThatNoLongerHoldsTheImage) {
  ASSERT_TRUE(base::CreateDirectory(Volume().image()));
  scoped_refptr<LaunchAttach> launch =
      LaunchAttach::Start(Volume(), tools_, DataVault::WatchdogPath());
  ASSERT_TRUE(launch->Wait(base::TimeDelta::Max()));
  std::unique_ptr<DataVault> vault = MakeVault(launch);
  // Detached since, as a volume refused as not encrypted is.
  tools_->DetachLeftovers();

  vault->EngineDidStop();
  EXPECT_EQ(tools_->calls(),
            (std::vector<std::string>{"attach-image", "watch", "leftovers"}));
}

// A deleted profile's data goes, its links and its folder in the volume, and
// only then does the deletion's record go; an id that is no folder's name
// removes nothing.
TEST_F(DataVaultTest, RemovesADeletedProfilesData) {
  std::unique_ptr<DataVault> vault = MakeVault();
  Open(*vault);
  ASSERT_TRUE(WaitFor([&] { return vault->is_attached(); }));
  MakeProfileFolder("beta");
  const base::FilePath link = data_.Append("Engine").Append("beta");
  ASSERT_TRUE(base::CreateSymbolicLink(
      Volume().Folder("beta").Append("Profile"), link));

  base::test::TestFuture<void> removed;
  vault->Remove("beta", removed.GetCallback());
  ASSERT_TRUE(WaitFor([&] { return removed.IsReady(); }));
  EXPECT_FALSE(base::PathExists(Volume().Folder("beta")));
  EXPECT_FALSE(ItemExists(link));

  bool removed_parent = false;
  vault->Remove("..",
                base::BindLambdaForTesting([&] { removed_parent = true; }));
  RunFor(base::Milliseconds(20));
  EXPECT_FALSE(removed_parent);
  EXPECT_TRUE(base::PathExists(Volume().mount_point()));
}

}  // namespace
}  // namespace pane::model
