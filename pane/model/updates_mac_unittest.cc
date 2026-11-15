// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/updates.h"

#include <optional>
#include <string_view>

#include "base/types/expected.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// The Mac's file is the arm64 dmg, the name every published Pane looks for, and
// the check takes it by default from a release that carries Windows' too.
TEST(UpdatesTest, TakesTheDmg) {
  EXPECT_EQ(std::string_view(kUpdateAssetSuffix), "-arm64.dmg");

  const base::expected<std::optional<AvailableUpdate>, UpdateCheckError>
      update = AvailableUpdateIn(
          R"({"tag_name":"v0.21.0","assets":[)"
          R"({"name":"Pane-0.21.0-x64.exe","browser_download_url":)"
          R"("https://github.com/andredezzy/pane/releases/download/v0.21.0/)"
          R"(Pane-0.21.0-x64.exe"},)"
          R"({"name":"Pane-0.21.0-arm64.dmg","browser_download_url":)"
          R"("https://github.com/andredezzy/pane/releases/download/v0.21.0/)"
          R"(Pane-0.21.0-arm64.dmg"}]})",
          "0.20.1");

  ASSERT_TRUE(update.has_value());
  ASSERT_TRUE(update->has_value());
  EXPECT_EQ((*update)->file_url.ExtractFileName(), "Pane-0.21.0-arm64.dmg");
}

}  // namespace
}  // namespace pane::model
