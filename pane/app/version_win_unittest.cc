// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/version.h"

#include <optional>
#include <string>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/scoped_path_override.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane {
namespace {

class VersionWinTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(folder_.CreateUniqueTempDir());
    assets_.emplace(base::DIR_ASSETS, folder_.GetPath());
  }

  void Write(const std::string& contents) {
    ASSERT_TRUE(base::WriteFile(
        folder_.GetPath().Append(FILE_PATH_LITERAL("pane-version")), contents));
  }

  base::ScopedTempDir folder_;
  std::optional<base::ScopedPathOverride> assets_;
};

// The package's pane-version, beside chrome.dll, is Pane's version, whatever
// space or line end it was written with.
TEST_F(VersionWinTest, ReadsThePackagesFile) {
  Write("0.21.0");
  EXPECT_EQ(BundledPaneVersion(), "0.21.0");
  EXPECT_EQ(PaneVersion(), "0.21.0");

  Write("0.21.0\r\n");
  EXPECT_EQ(PaneVersion(), "0.21.0");
}

// A build with none, out/Win's or a test copy's from it, reads 0.0.0, as the
// Mac's out/Release does; an empty file and one too long for a version are
// none.
TEST_F(VersionWinTest, NoneReadsZero) {
  EXPECT_EQ(BundledPaneVersion(), std::nullopt);
  EXPECT_EQ(PaneVersion(), "0.0.0");

  Write(" \n");
  EXPECT_EQ(PaneVersion(), "0.0.0");

  Write(std::string(100, '1'));
  EXPECT_EQ(PaneVersion(), "0.0.0");
}

}  // namespace
}  // namespace pane
