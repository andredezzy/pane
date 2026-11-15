// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/updates.h"

#include <optional>
#include <string_view>

#include "base/types/expected.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Windows' file is the x64 installer, and the check takes it by default from a
// release that carries the Mac's dmg too: never the dmg.
TEST(UpdatesTest, TakesTheInstaller) {
  EXPECT_EQ(std::string_view(kUpdateAssetSuffix), "-x64.exe");

  const base::expected<std::optional<AvailableUpdate>, UpdateCheckError>
      update = AvailableUpdateIn(
          R"({"tag_name":"v0.21.0","assets":[)"
          R"({"name":"Pane-0.21.0-arm64.dmg","browser_download_url":)"
          R"("https://github.com/andredezzy/pane/releases/download/v0.21.0/)"
          R"(Pane-0.21.0-arm64.dmg"},)"
          R"({"name":"Pane-0.21.0-x64.exe","browser_download_url":)"
          R"("https://github.com/andredezzy/pane/releases/download/v0.21.0/)"
          R"(Pane-0.21.0-x64.exe"}]})",
          "0.20.1");

  ASSERT_TRUE(update.has_value());
  ASSERT_TRUE(update->has_value());
  EXPECT_EQ((*update)->file_url.ExtractFileName(), "Pane-0.21.0-x64.exe");
}

}  // namespace
}  // namespace pane::model
