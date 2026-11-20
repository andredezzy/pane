// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/removal.h"

#include <windows.h>

#include <optional>

#include "base/base_paths.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/test/scoped_path_override.h"
#include "pane/installer/files.h"
#include "pane/installer/switches.h"
#include "pane/installer/update_folder.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::installer {

namespace {

// The uninstall's removal (removal.h), with %LOCALAPPDATA% a folder of the
// test's own and chrome.exe's unregister a stand-in that counts its runs.
class RemovalTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_.CreateUniqueTempDir());
    local_.emplace(base::DIR_LOCAL_APP_DATA, temp_.GetPath());
    installed_ = AppFolder();
    ASSERT_EQ(installed_,
              temp_.GetPath().Append(L"Programs\\Pane\\Application"));
    Lay(installed_);
  }

  // An app folder as an install lays one out, in part: chrome.exe, the
  // helper, and a file in a folder.
  static void Lay(const base::FilePath& app) {
    ASSERT_TRUE(base::CreateDirectory(app.Append(L"locales")));
    ASSERT_TRUE(base::WriteFile(app.Append(L"chrome.exe"), "MZ"));
    ASSERT_TRUE(base::WriteFile(app.Append(L"pane_setup.exe"), "MZ"));
    ASSERT_TRUE(base::WriteFile(app.Append(L"locales\\en-US.pak"), "pak"));
  }

  RunUnregister Unregisters(int code) {
    return base::BindOnce(
        [](int* runs, int code, const base::FilePath&) -> std::optional<int> {
          ++*runs;
          return code;
        },
        &runs_, code);
  }

  base::ScopedTempDir temp_;
  std::optional<base::ScopedPathOverride> local_;
  base::FilePath installed_;
  int runs_ = 0;
};

}  // namespace

// %LOCALAPPDATA%\Programs\Pane\Application alone, as NTFS compares names: no
// other folder, nor the install named another way.
TEST_F(RemovalTest, KnowsTheInstallsOwnFolder) {
  EXPECT_TRUE(IsInstalledAppFolder(installed_));
  EXPECT_TRUE(IsInstalledAppFolder(
      base::FilePath(base::ToUpperASCII(installed_.value()))));
  EXPECT_TRUE(IsInstalledAppFolder(installed_.AsEndingWithSeparator()));
  EXPECT_FALSE(IsInstalledAppFolder(installed_.DirName()));
  EXPECT_FALSE(IsInstalledAppFolder(installed_.Append(L"locales")));
  EXPECT_FALSE(IsInstalledAppFolder(installed_.Append(L"..\\Application")));
  EXPECT_FALSE(
      IsInstalledAppFolder(temp_.GetPath().Append(L"Copy\\Pane\\Application")));
  EXPECT_FALSE(IsInstalledAppFolder(temp_.GetPath().Append(L"out\\Win")));
  EXPECT_FALSE(IsInstalledAppFolder(base::FilePath()));
}

// A folder that isn't the install, holding a chrome.exe as a build's or a
// user's copy does, with an update beside it, is never unregistered or
// deleted: not on the uninstall's first run, nor on a Try again after an
// unregister, the two ways into the removal (flow.cc's Remove).
TEST_F(RemovalTest, NeverRemovesAnotherFolder) {
  const base::FilePath copy =
      temp_.GetPath().Append(L"Copy\\Pane\\Application");
  const base::FilePath build = temp_.GetPath().Append(L"out\\Win");
  const base::FilePath named_otherwise = installed_.Append(L"..\\Application");
  for (const base::FilePath& other : {copy, build, named_otherwise}) {
    SCOPED_TRACE(other.value());
    Lay(other);
    ASSERT_TRUE(base::CreateDirectory(UpdateFolderOf(other).Append(L"9.0.0")));
    for (const bool unregistered : {false, true}) {
      const RemovalResult result = RemovePane(
          other, unregistered, Unregisters(kSwitchDone), FileRemoved());
      EXPECT_EQ(result.outcome, RemovalOutcome::kRefused);
      EXPECT_EQ(runs_, 0);
      EXPECT_TRUE(base::PathExists(other.Append(L"chrome.exe")));
      EXPECT_TRUE(base::PathExists(other.Append(L"locales\\en-US.pak")));
      EXPECT_TRUE(base::PathExists(UpdateFolderOf(other).Append(L"9.0.0")));
    }
  }
  EXPECT_TRUE(base::PathExists(installed_.Append(L"chrome.exe")));
}

// The install's own: unregistered once, then every file, told for the bar,
// the update staged beside it, and the folders Pane made.
TEST_F(RemovalTest, RemovesTheInstall) {
  ASSERT_TRUE(
      base::CreateDirectory(UpdateFolderOf(installed_).Append(L"99.0.0")));
  uint64_t removed = 0;
  const RemovalResult result =
      RemovePane(installed_, /*unregistered=*/false, Unregisters(kSwitchDone),
                 base::BindRepeating(
                     [](uint64_t* removed, uint64_t size) { *removed += size; },
                     &removed));
  EXPECT_EQ(result.outcome, RemovalOutcome::kRemoved);
  EXPECT_TRUE(result.unregistered);
  EXPECT_EQ(runs_, 1);
  EXPECT_EQ(removed, 2u + 2u + 3u);
  EXPECT_FALSE(base::PathExists(InstallFolder()));
  EXPECT_FALSE(base::PathExists(ProgramsFolder()));
}

// An unregister that failed, or a Pane holding the data, leaves every file:
// the uninstall runs again from Installed apps, whose entry stays
// (windows_integration.h).
TEST_F(RemovalTest, LeavesTheFilesUntilUnregistered) {
  EXPECT_EQ(
      RemovePane(installed_, false, Unregisters(kSwitchPaneOpen), FileRemoved())
          .outcome,
      RemovalOutcome::kPaneOpen);
  const RemovalResult failed =
      RemovePane(installed_, false, Unregisters(static_cast<int>(0x80070005)),
                 FileRemoved());
  EXPECT_EQ(failed.outcome, RemovalOutcome::kNotUnregistered);
  EXPECT_EQ(failed.error, 0x80070005u);
  EXPECT_FALSE(failed.unregistered);
  EXPECT_EQ(runs_, 2);
  EXPECT_TRUE(base::PathExists(installed_.Append(L"chrome.exe")));
  EXPECT_TRUE(base::PathExists(installed_.Append(L"locales\\en-US.pak")));
}

// A file another program holds stays, and says so (the first installer's "Try
// again" had no button behind it, until commit 4fb6caa): a Try again then
// removes what is left, with chrome.exe gone already and nothing unregistered
// twice.
TEST_F(RemovalTest, TryAgainRemovesWhatIsLeft) {
  base::File held(installed_.Append(L"locales\\en-US.pak"),
                  base::File::FLAG_OPEN | base::File::FLAG_READ);
  ASSERT_TRUE(held.IsValid());
  const RemovalResult left =
      RemovePane(installed_, false, Unregisters(kSwitchDone), FileRemoved());
  EXPECT_EQ(left.outcome, RemovalOutcome::kFilesLeft);
  EXPECT_TRUE(left.unregistered);
  EXPECT_NE(left.error, static_cast<DWORD>(ERROR_SUCCESS));
  EXPECT_FALSE(base::PathExists(installed_.Append(L"chrome.exe")));
  EXPECT_TRUE(base::PathExists(installed_.Append(L"locales\\en-US.pak")));

  held.Close();
  const RemovalResult again = RemovePane(
      installed_, left.unregistered, Unregisters(kSwitchDone), FileRemoved());
  EXPECT_EQ(again.outcome, RemovalOutcome::kRemoved);
  EXPECT_EQ(runs_, 1);
  EXPECT_FALSE(base::PathExists(InstallFolder()));
}

}  // namespace pane::installer
