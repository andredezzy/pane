// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "pane/model/mac/data_lock.h"
#include "pane/model/mac/data_volume.h"
#include "pane/model/mac/vault_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// pane-volume-watch, as built beside these tests, on an image each test makes
// in a folder of its own: the Swift's watchdog (app/VolumeWatch) had no
// tests, the security scenario killed Pane under it.

class VolumeWatchTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    data_ = directory_.GetPath().Append("data");
    ASSERT_TRUE(base::CreateDirectory(data_));
    base::FilePath executables;
    ASSERT_TRUE(base::PathService::Get(base::DIR_EXE, &executables));
    watchdog_ = executables.Append("pane-volume-watch");
    ASSERT_TRUE(base::PathExists(watchdog_));
  }

  // Detaches only this test's image, found by its path, and lets the next
  // test that attaches one go on.
  void TearDown() override {
    EndPane();
    if (volume_) {
      volume_->DetachLeftovers();
      EXPECT_FALSE(volume_->Holds(device_));
    }
  }

  // The test's image, attached with its volume locked, as the launch leaves
  // it; and a process standing in for the Pane it belongs to.
  void AttachImage() {
    images_lock_ = LockImagesForTesting();
    volume_.emplace(data_, std::nullopt);
    const base::expected<DataVolume::Devices, VolumeError> devices =
        volume_->AttachOrMake("pass");
    ASSERT_TRUE(devices.has_value()) << devices.error().ToString();
    device_ = devices->image;
    ASSERT_TRUE(volume_->Holds(device_));
    pane_ = base::LaunchProcess({"/bin/sleep", "60"}, base::LaunchOptions());
    ASSERT_TRUE(pane_.IsValid());
  }

  // Pane's process ends, and is reaped: its number may be another's after.
  void EndPane() {
    if (pane_.IsValid()) {
      pane_.Terminate(0, /*wait=*/true);
      pane_ = base::Process();
    }
  }

  base::Process Watch(const std::vector<std::string>& arguments) {
    std::vector<std::string> argv = {watchdog_.value()};
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    return base::LaunchProcess(argv, base::LaunchOptions());
  }

  // Its exit status, once it ends within 30 s.
  static std::optional<int> Ended(base::Process& process) {
    int status = -1;
    if (!process.WaitForExitWithTimeout(base::Seconds(30), &status)) {
      process.Terminate(0, /*wait=*/true);
      return std::nullopt;
    }
    return status;
  }

  base::ScopedTempDir directory_;
  base::FilePath data_;
  base::FilePath watchdog_;
  base::ScopedFD images_lock_;
  std::optional<DataVolume> volume_;
  std::string device_;
  base::Process pane_;
};

// It waits for Pane's process, not for the image, and once that process ends,
// a crash included, it detaches the image, found by its path.
TEST_F(VolumeWatchTest, DetachesTheImageOnceItsPaneEnds) {
  AttachImage();
  base::Process watchdog =
      Watch({base::NumberToString(pane_.Pid()), volume_->image().value()});
  ASSERT_TRUE(watchdog.IsValid());
  int status = -1;
  EXPECT_FALSE(
      watchdog.WaitForExitWithTimeout(base::Milliseconds(300), &status));
  EXPECT_TRUE(volume_->Holds(device_));

  EndPane();
  EXPECT_EQ(Ended(watchdog), 0);
  EXPECT_FALSE(volume_->Holds(device_));
}

// A Pane that took the data's lock since holds the image now: it stays.
TEST_F(VolumeWatchTest, LeavesTheImageToTheNextPane) {
  AttachImage();
  base::Process watchdog =
      Watch({base::NumberToString(pane_.Pid()), volume_->image().value()});
  ASSERT_TRUE(watchdog.IsValid());
  std::unique_ptr<DataLock> next = DataLock::Take(data_);
  ASSERT_TRUE(next);

  EndPane();
  EXPECT_EQ(Ended(watchdog), 0);
  EXPECT_TRUE(volume_->Holds(device_));
}

// It takes a process and Pane's image, and nothing else.
TEST_F(VolumeWatchTest, TakesAPidAndTheImage) {
  const std::string image = DataVolume(data_, std::nullopt).image().value();
  for (const std::vector<std::string>& arguments :
       std::vector<std::vector<std::string>>{
           {},
           {"1"},
           {"pane", image},
           {"0", image},
           {"1", data_.Append("Other.sparsebundle").value()},
           {"1", image, "more"}}) {
    base::Process watchdog = Watch(arguments);
    ASSERT_TRUE(watchdog.IsValid());
    EXPECT_EQ(Ended(watchdog), 64);
  }
}

}  // namespace
}  // namespace pane::model
