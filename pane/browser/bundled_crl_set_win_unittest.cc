// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/bundled_crl_set.h"

#include <optional>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/scoped_path_override.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane {
namespace {

// The package's crl-set beside chrome.dll is the one the engine loads; a
// build with none, out/Win's, loads none.
TEST(BundledCRLSetWinTest, BesideChromeDll) {
  base::ScopedTempDir folder;
  ASSERT_TRUE(folder.CreateUniqueTempDir());
  base::ScopedPathOverride assets(base::DIR_ASSETS, folder.GetPath());

  EXPECT_EQ(BundledCRLSet(), std::nullopt);

  const base::FilePath crl_set =
      folder.GetPath().Append(FILE_PATH_LITERAL("crl-set"));
  ASSERT_TRUE(base::WriteFile(crl_set, "CRLSet"));
  EXPECT_EQ(BundledCRLSet(), crl_set);
}

}  // namespace
}  // namespace pane
