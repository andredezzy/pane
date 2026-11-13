// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/engine_keys.h"

#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::encryption {
namespace {

// A user data dir reached through a link, as /var is /private/var: its roots
// are it and its resolved form, each with the volume's mount point in it.
TEST(EngineKeysMacTest, TheRootsHoldTheResolvedFormAndTheVolume) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath real =
      base::MakeAbsoluteFilePath(temp.GetPath()).AppendASCII("Real");
  ASSERT_TRUE(base::CreateDirectory(real.AppendASCII("Engine")));
  const base::FilePath link = temp.GetPath().AppendASCII("Link");
  ASSERT_TRUE(base::CreateSymbolicLink(real, link));

  const base::FilePath given = link.AppendASCII("Engine");
  const base::FilePath resolved = real.AppendASCII("Engine");
  EXPECT_EQ(RootsOf(given), (std::vector<base::FilePath>{
                                given, given.AppendASCII("Volume"), resolved,
                                resolved.AppendASCII("Volume")}));
}

// Names compare byte for byte on the Mac, as they always have.
TEST(EngineKeysMacTest, NamesCompareByteForByte) {
  EXPECT_TRUE(SameComponent("Engine", "Engine"));
  EXPECT_FALSE(SameComponent("Engine", "engine"));
}

}  // namespace
}  // namespace pane::encryption
