// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/update_files.h"

#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/test/test_timeouts.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane {
namespace {

// Whether `path` appears within `limit`.
bool Appears(const base::FilePath& path, base::TimeDelta limit) {
  const base::TimeTicks end = base::TimeTicks::Now() + limit;
  while (base::TimeTicks::Now() < end) {
    if (base::PathExists(path)) {
      return true;
    }
    base::PlatformThread::Sleep(base::Milliseconds(20));
  }
  return base::PathExists(path);
}

// The dmg opens only once the process that asked has exited: while it lives,
// nothing opens it; once it is gone, the opener runs with the file.
TEST(UpdateFilesTest, TheFileOpensOnlyOnceTheProcessHasExited) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath opened = dir.GetPath().Append("opened");
  // An opener that writes the file it was given where the test reads it.
  const base::FilePath opener = dir.GetPath().Append("opener");
  ASSERT_TRUE(base::WriteFile(
      opener, "#!/bin/sh\nprintf '%s' \"$1\" > \"" + opened.value() + "\"\n"));
  ASSERT_TRUE(base::SetPosixFilePermissions(opener, 0700));

  base::Process quitting =
      base::LaunchProcess({"/bin/sleep", "30"}, base::LaunchOptions());
  ASSERT_TRUE(quitting.IsValid());
  const base::FilePath dmg = dir.GetPath().Append("Pane-0.20.0-arm64.dmg");
  ASSERT_TRUE(OpenAfterProcessExits(quitting.Pid(), dmg, opener));

  EXPECT_FALSE(Appears(opened, base::Milliseconds(500)));

  ASSERT_TRUE(quitting.Terminate(0, /*wait=*/true));
  ASSERT_TRUE(Appears(opened, TestTimeouts::action_timeout()));
  std::string given;
  ASSERT_TRUE(base::ReadFileToString(opened, &given));
  EXPECT_EQ(given, dmg.value());
}

}  // namespace
}  // namespace pane
