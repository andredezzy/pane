// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/engine_folder.h"

#include <string>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/scoped_environment_variable_override.h"
#include "chrome/install_static/install_modes.h"
#include "chrome/install_static/user_data_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// The folder chrome_elf names for a launch started with `command_line`, which
// Chromium's ChromeMainDelegate takes as its user data dir on Windows, in
// Pane's install mode (patch 0001).
base::FilePath ChromeElfFolder(const std::wstring& command_line) {
  std::wstring folder;
  std::wstring unusable;
  EXPECT_TRUE(install_static::GetUserDataDirectoryImpl(
      command_line, install_static::kInstallModes[0], &folder, &unusable));
  EXPECT_EQ(unusable, std::wstring());
  return base::FilePath(folder);
}

// The user's launch, a URL and no --user-data-dir, as the shell starts Pane
// with another app's link: chrome_elf names the engine folder of the data
// folder in the user's local application data, %LOCALAPPDATA%\Pane.
TEST(EngineFolderTest, ChromeElfNamesItForTheUsersLaunch) {
  base::ScopedTempDir local;
  ASSERT_TRUE(local.CreateUniqueTempDir());
  base::ScopedEnvironmentVariableOverride local_app_data(
      "LOCALAPPDATA", local.GetPath().AsUTF8Unsafe());

  EXPECT_EQ(ChromeElfFolder(L"chrome.exe https://example.com/"),
            EngineFolder(local.GetPath().Append(FILE_PATH_LITERAL("Pane"))));
}

// A test copy names its data's own with the switch, quoted as a launch's
// argument is: its name holds a space.
TEST(EngineFolderTest, ChromeElfTakesATestCopysOwn) {
  base::ScopedTempDir data;
  ASSERT_TRUE(data.CreateUniqueTempDir());
  const base::FilePath engine = EngineFolder(data.GetPath());

  EXPECT_EQ(
      ChromeElfFolder(L"chrome.exe --use-mock-keychain \"--user-data-dir=" +
                      engine.value() + L"\""),
      engine);
}

}  // namespace
}  // namespace pane::model
