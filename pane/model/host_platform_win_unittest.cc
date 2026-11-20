// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/host_platform.h"

#include <optional>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Windows' promise for the data in Settings (U5, Paper 11.19): the chip's
// part only where it guards the PIN, the rest the same.
TEST(HostPlatformTest, PromiseNamesTheChipOnlyWhereItGuards) {
  const std::optional<std::string> chip = HostDataPromise(true);
  const std::optional<std::string> none = HostDataPromise(false);
  ASSERT_TRUE(chip && none);
  EXPECT_EQ(*chip,
            "Without a PIN, any app you run can open Pane’s data; with "
            "one, the PC’s security chip guards it. Files’ sizes and "
            "times and extensions’ code still show on disk, and while "
            "Pane is unlocked, apps can read its memory.");
  EXPECT_EQ(*none,
            "Without a PIN, any app you run can open Pane’s data. "
            "Files’ sizes and times and extensions’ code still show "
            "on disk, and while Pane is unlocked, apps can read its memory.");
}

}  // namespace
}  // namespace pane::model
