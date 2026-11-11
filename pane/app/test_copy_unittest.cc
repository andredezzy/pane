// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/test_copy.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane {
namespace {

// Pane's data folder under a home, as DefaultDataDirectory names it.
base::FilePath DataUnder(const base::FilePath& home) {
  return home.Append(FILE_PATH_LITERAL("Library"))
      .Append(FILE_PATH_LITERAL("Application Support"))
      .Append(FILE_PATH_LITERAL("xyz.roundedcorners.pane"));
}

class LaunchKindTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temporary_.CreateUniqueTempDir());
    base::FilePath folder;
    ASSERT_TRUE(base::GetTempDir(&folder));
    temporary_folder_ = base::MakeAbsoluteFilePath(folder);
    ASSERT_FALSE(temporary_folder_.empty());
    // Beside the temporary folder, as the system's cache folder is, and
    // never made: a home that stands for the user's, read as the launch
    // reads it.
    outside_ =
        temporary_folder_.DirName().Append(FILE_PATH_LITERAL("pane-test-home"));
    named_ = temporary_.GetPath().Append(FILE_PATH_LITERAL("data"));
    test_home_ = temporary_.GetPath().Append(FILE_PATH_LITERAL("home"));
  }

  base::ScopedTempDir temporary_;
  base::FilePath temporary_folder_;
  base::FilePath outside_;
  base::FilePath named_;
  base::FilePath test_home_;
};

// The user's own launch: no switch, its data under their home.
TEST_F(LaunchKindTest, UsersLaunchOpensTheirData) {
  EXPECT_EQ(LaunchKind::kUser,
            KindOfLaunch(/*mock_keychain=*/false, base::FilePath(),
                         DataUnder(outside_)));
  // PANE_DATA_DIR, the hooks off, names no folder of the user's to open: the
  // data stays the one under the home, as the Swift's did.
  EXPECT_EQ(LaunchKind::kUser,
            KindOfLaunch(/*mock_keychain=*/false,
                         outside_.Append(FILE_PATH_LITERAL("data")),
                         DataUnder(outside_)));
}

// A scenario's launch, by either name it gives its data.
TEST_F(LaunchKindTest, TestCopyRunsOnTheMockKeychain) {
  EXPECT_EQ(LaunchKind::kTestCopy,
            KindOfLaunch(/*mock_keychain=*/true, named_, DataUnder(outside_)));
  EXPECT_EQ(LaunchKind::kTestCopy, KindOfLaunch(/*mock_keychain=*/true, named_,
                                                DataUnder(test_home_)));
  EXPECT_EQ(LaunchKind::kTestCopy,
            KindOfLaunch(/*mock_keychain=*/true, base::FilePath(),
                         DataUnder(test_home_)));
}

// A test copy without the switch, as 0.19.1's installer relaunches one with
// its PANE_ variables and its home, or as a scenario's launch might: either
// name in the temporary folder refuses it, so it never reaches the user's
// Keychain item.
TEST_F(LaunchKindTest, TestCopyWithoutMockKeychainRefused) {
  EXPECT_EQ(LaunchKind::kTestCopyWithoutMockKeychain,
            KindOfLaunch(/*mock_keychain=*/false, named_, DataUnder(outside_)));
  EXPECT_EQ(LaunchKind::kTestCopyWithoutMockKeychain,
            KindOfLaunch(/*mock_keychain=*/false, base::FilePath(),
                         DataUnder(test_home_)));
  EXPECT_EQ(
      LaunchKind::kTestCopyWithoutMockKeychain,
      KindOfLaunch(/*mock_keychain=*/false, named_, DataUnder(test_home_)));
  EXPECT_EQ(LaunchKind::kTestCopyWithoutMockKeychain,
            KindOfLaunch(/*mock_keychain=*/false,
                         outside_.Append(FILE_PATH_LITERAL("data")),
                         DataUnder(test_home_)));
}

// The mock keychain on data outside the temporary folder: the user's own data
// root under their home, or a folder PANE_DATA_DIR names there, whatever the
// home.
TEST_F(LaunchKindTest, MockKeychainOutsideTestRefused) {
  EXPECT_EQ(LaunchKind::kMockKeychainOutsideTest,
            KindOfLaunch(/*mock_keychain=*/true, base::FilePath(),
                         DataUnder(outside_)));
  EXPECT_EQ(LaunchKind::kMockKeychainOutsideTest,
            KindOfLaunch(/*mock_keychain=*/true,
                         outside_.Append(FILE_PATH_LITERAL("data")),
                         DataUnder(test_home_)));
  // A relative name, or one that climbs out, is in no folder at all.
  EXPECT_EQ(LaunchKind::kMockKeychainOutsideTest,
            KindOfLaunch(/*mock_keychain=*/true,
                         base::FilePath(FILE_PATH_LITERAL("data")),
                         DataUnder(test_home_)));
  EXPECT_EQ(LaunchKind::kMockKeychainOutsideTest,
            KindOfLaunch(/*mock_keychain=*/true,
                         named_.Append(FILE_PATH_LITERAL(".."))
                             .Append(FILE_PATH_LITERAL(".."))
                             .Append(FILE_PATH_LITERAL("data")),
                         DataUnder(test_home_)));
}

// What lies in the temporary folder, made yet or not; the folder itself, and
// what lies beside it, don't.
TEST_F(LaunchKindTest, InTemporaryFolder) {
  EXPECT_TRUE(InTemporaryFolder(named_));
  EXPECT_TRUE(InTemporaryFolder(DataUnder(test_home_)));
  EXPECT_TRUE(InTemporaryFolder(temporary_.GetPath()));
  EXPECT_FALSE(InTemporaryFolder(temporary_folder_));
  EXPECT_FALSE(InTemporaryFolder(outside_));
  EXPECT_FALSE(InTemporaryFolder(DataUnder(outside_)));
}

}  // namespace
}  // namespace pane
