// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/files.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::installer {

// Windows' binary units, as Explorer and Installed apps show sizes (Paper
// 11.12.7's "490 MB", 11.12.9's "3.4 GB", 11.12.10's "1.3 of 3.4 GB").
TEST(FilesTest, SizesAsWindowsShowsThem) {
  EXPECT_EQ(SizeText(0), u"0 KB");
  EXPECT_EQ(SizeText(12 * 1024), u"12 KB");
  EXPECT_EQ(SizeText(494198542), u"471 MB");
  EXPECT_EQ(SizeText(212ull * 1024 * 1024), u"212 MB");
  EXPECT_EQ(SizeText(3651565158ull), u"3.4 GB");
  EXPECT_EQ(SizeOfText(1395864371ull, 3651565158ull), u"1.3 of 3.4 GB");
  EXPECT_EQ(SizeOfText(5ull * 1024 * 1024, 300ull * 1024 * 1024),
            u"5 of 300 MB");
  // Never past the whole.
  EXPECT_EQ(SizeOfText(4000000000ull, 3651565158ull), u"3.4 of 3.4 GB");
}

// The folder and the update it will stage beside it, and a margin.
TEST(FilesTest, SpaceForTwoCopies) {
  EXPECT_EQ(SpaceNeeded(494198542), 2 * 494198542ull + 64ull * 1024 * 1024);
}

TEST(FilesTest, FoldersComparedAsNtfsDoes) {
  const base::FilePath app(
      L"C:\\Users\\Ana\\AppData\\Local\\Programs\\Pane\\"
      L"Application");
  EXPECT_TRUE(IsInFolder(app.Append(L"chrome.exe"), app));
  EXPECT_TRUE(IsInFolder(base::FilePath(L"c:\\users\\ana\\appdata\\local\\"
                                        L"programs\\pane\\application\\"
                                        L"chrome.exe"),
                         app));
  EXPECT_TRUE(IsInFolder(app, app));
  EXPECT_TRUE(IsInFolder(base::FilePath(app.value() + L"\\"), app));
  // A neighbour whose name starts the same is not in it.
  EXPECT_FALSE(IsInFolder(base::FilePath(app.value() + L"2\\chrome.exe"), app));
  EXPECT_FALSE(IsInFolder(base::FilePath(L"C:\\Windows\\notepad.exe"), app));
  EXPECT_FALSE(IsInFolder(app, base::FilePath()));
}

TEST(FilesTest, DriveLetters) {
  EXPECT_EQ(DriveOf(base::FilePath(L"c:\\Users\\Ana")), u"C:");
  EXPECT_EQ(DriveOf(base::FilePath(L"D:\\")), u"D:");
}

TEST(FilesTest, VersionFromTheAppFolder) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  EXPECT_FALSE(VersionIn(temp.GetPath()));
  ASSERT_TRUE(
      base::WriteFile(temp.GetPath().Append(L"pane-version"), "0.21.0\n"));
  EXPECT_EQ(VersionIn(temp.GetPath()), base::Version("0.21.0"));
  ASSERT_TRUE(base::WriteFile(temp.GetPath().Append(L"pane-version"), "x"));
  EXPECT_FALSE(VersionIn(temp.GetPath()));
}

TEST(FilesTest, FolderSizeCountsEveryFile) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(temp.GetPath().Append(L"a"), "12345"));
  ASSERT_TRUE(base::CreateDirectory(temp.GetPath().Append(L"locales")));
  ASSERT_TRUE(
      base::WriteFile(temp.GetPath().Append(L"locales").Append(L"b"), "123"));
  EXPECT_EQ(FolderSize(temp.GetPath()), 8u);
}

}  // namespace pane::installer
