// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/update_files.h"

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane {

namespace {

// The file's Mark of the Web, its Zone.Identifier stream, or nothing.
std::optional<std::string> MarkOf(const base::FilePath& file) {
  std::string mark;
  if (!base::ReadFileToString(
          base::FilePath(file.value() + L":Zone.Identifier"), &mark)) {
    return std::nullopt;
  }
  return mark;
}

}  // namespace

// An installer the release's digest didn't vouch for keeps the Mark of the
// Web a download takes, the Internet zone and no address (patch 0008's), so
// SmartScreen asks before it runs; a verified one has none.
TEST(UpdateFilesTest, OnlyAnUnverifiedInstallerKeepsTheMarkOfTheWeb) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath file = temp.GetPath().Append(L"Pane-99.0.0-x64.exe");
  ASSERT_TRUE(base::WriteFile(file, "installer"));
  EXPECT_FALSE(MarkOf(file));

  Quarantine(file);
  EXPECT_EQ(MarkOf(file),
            "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=about:internet\r\n");
  // The file itself as it was.
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(file, &contents));
  EXPECT_EQ(contents, "installer");

  EXPECT_TRUE(ReleaseFromQuarantine(file));
  EXPECT_FALSE(MarkOf(file));
  // Released again, or never marked: nothing to do.
  EXPECT_TRUE(ReleaseFromQuarantine(file));

  // The mark moves with the file into the downloads folder.
  Quarantine(file);
  const base::FilePath saved = temp.GetPath().Append(L"saved.exe");
  ASSERT_TRUE(base::Move(file, saved));
  EXPECT_TRUE(MarkOf(saved));
}

}  // namespace pane
