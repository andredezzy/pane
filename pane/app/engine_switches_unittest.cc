// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/engine_switches.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/strings/string_split.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane {
namespace {

// The features a feature list switch names.
std::vector<std::string> Features(const base::CommandLine& command_line,
                                  const char* list) {
  return base::SplitString(command_line.GetSwitchValueASCII(list), ",",
                           base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
}

// A launch's command line after Pane's switches, from `switches` ("name" or
// "name=value").
base::CommandLine Launch(const std::vector<std::string>& switches) {
  base::CommandLine command_line(base::FilePath(FILE_PATH_LITERAL("/Pane")));
  for (const std::string& name : switches) {
    const size_t equals = name.find('=');
    if (equals == std::string::npos) {
      command_line.AppendSwitch(name);
    } else {
      command_line.AppendSwitchASCII(name.substr(0, equals),
                                     name.substr(equals + 1));
    }
  }
  SetEngineSwitches(command_line, base::FilePath(FILE_PATH_LITERAL("/data")));
  return command_line;
}

// Every launch's views raster on the CPU and shape their text over the
// fonts' own tables, the user's and a scenario's alike.
TEST(EngineSwitchesTest, ViewsRasterOnTheCpuOverTheFontsOwnTables) {
  for (const base::CommandLine& command_line :
       {Launch({}), Launch({"use-mock-keychain"})}) {
    EXPECT_TRUE(std::ranges::contains(Features(command_line, "enable-features"),
                                      "HarfBuzzZeroCopyFontTable"));
    EXPECT_TRUE(std::ranges::contains(
        Features(command_line, "disable-features"), "UiGpuRasterization"));
  }
}

// A scenario's own features stay beside Pane's, so a probe can turn one of
// Pane's off: Chromium's feature list reads a disabled feature over an
// enabled one.
TEST(EngineSwitchesTest, ScenarioKeepsItsFeatures) {
  const base::CommandLine command_line =
      Launch({"use-mock-keychain", "enable-features=Probe",
              "disable-features=HarfBuzzZeroCopyFontTable"});
  const std::vector<std::string> enabled =
      Features(command_line, "enable-features");
  EXPECT_TRUE(std::ranges::contains(enabled, "Probe"));
  EXPECT_TRUE(std::ranges::contains(enabled, "HarfBuzzZeroCopyFontTable"));
  EXPECT_TRUE(std::ranges::contains(Features(command_line, "disable-features"),
                                    "HarfBuzzZeroCopyFontTable"));
}

// The user's launch keeps none of Chrome's switches it was handed, only
// Pane's own.
TEST(EngineSwitchesTest, UsersLaunchTakesPanesFeaturesAlone) {
  const base::CommandLine command_line = Launch({"enable-features=Probe"});
  const std::vector<std::string> enabled =
      Features(command_line, "enable-features");
  EXPECT_FALSE(std::ranges::contains(enabled, "Probe"));
  EXPECT_TRUE(std::ranges::contains(enabled, "HarfBuzzZeroCopyFontTable"));
  EXPECT_TRUE(std::ranges::contains(enabled, "DisableTabDiscarding"));
}

}  // namespace
}  // namespace pane
