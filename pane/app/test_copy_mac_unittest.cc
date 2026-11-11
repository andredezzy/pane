// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/test_copy.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane {
namespace {

// Where a path leads, not how it reads: a link in the temporary folder to the
// home, or to any folder outside it, is outside, and a test copy's data there
// is refused the mock keychain.
TEST(LaunchKindMacTest, InTemporaryFolderFollowsLinks) {
  base::ScopedTempDir temporary;
  ASSERT_TRUE(temporary.CreateUniqueTempDir());
  const base::FilePath link =
      temporary.GetPath().Append(FILE_PATH_LITERAL("link"));
  ASSERT_TRUE(base::CreateSymbolicLink(base::FilePath("/var/empty"), link));

  EXPECT_FALSE(InTemporaryFolder(link));
  EXPECT_FALSE(InTemporaryFolder(link.Append("data")));
  EXPECT_EQ(LaunchKind::kMockKeychainOutsideTest,
            KindOfLaunch(/*mock_keychain=*/true, link.Append("data"),
                         base::FilePath("/var/empty/data")));

  // A link that leads nowhere resolves to nothing.
  const base::FilePath dangling =
      temporary.GetPath().Append(FILE_PATH_LITERAL("dangling"));
  ASSERT_TRUE(base::CreateSymbolicLink(
      temporary.GetPath().Append(FILE_PATH_LITERAL("gone")), dangling));
  EXPECT_FALSE(InTemporaryFolder(dangling));
}

}  // namespace
}  // namespace pane
