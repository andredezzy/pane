// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/engine_keys.h"

#include <string>

#include "base/files/file_path.h"
#include "base/test/gtest_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::encryption {
namespace {

constexpr char kID[] = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02";
constexpr char kOtherID[] = "aaaaaaaa-8b3e-4c7d-9a2f-1e5b7c3d9f02";

class EngineKeysTest : public testing::Test {
 protected:
  void SetUp() override {
    ResetForTesting();
    AddRoots({base::FilePath("/data/Engine"), base::FilePath("/data/Engine/Volume"),
              base::FilePath("/Caches/pane/Engine")});
  }
  void TearDown() override { ResetForTesting(); }

  void Register(const std::string& id) {
    Key key;
    key.fill(7);
    RegisterKey(id, key);
  }
};

TEST_F(EngineKeysTest, EveryFormOfAProfilesPathIsItsKeys) {
  Register(kID);

  for (const std::string& path :
       {std::string("/data/Engine/") + kID + "/History",
        std::string("/data/Engine/Volume/") + kID + "/Profile/History",
        std::string("/data/Engine/Volume/") + kID + "/Cache/Cache_Data/index",
        std::string("/Caches/pane/Engine/") + kID + "/Cache/Cache_Data/index"}) {
    EXPECT_TRUE(KeysFor(base::FilePath(path))) << path;
  }
}

TEST_F(EngineKeysTest, WhatIsNotAProfilesFileStaysPlain) {
  Register(kID);

  for (const std::string& path :
       {"/data/Engine/Local State", "/data/Engine/Default/History",
        "/data/Engine/Safe Browsing/Bloom", "/data/settings.json",
        "/elsewhere/History", "/data/Engine/Volume/.fseventsd/0001"}) {
    EXPECT_FALSE(KeysFor(base::FilePath(path))) << path;
  }
}

TEST_F(EngineKeysTest, ExtensionsCodeStaysPlain) {
  Register(kID);
  EXPECT_FALSE(KeysFor(base::FilePath(std::string("/data/Engine/") + kID +
                                      "/Extensions/abc/1.0/manifest.json")));
  EXPECT_FALSE(KeysFor(base::FilePath(std::string("/data/Engine/Volume/") + kID +
                                      "/Profile/Extensions/abc/1.0/a.js")));
  // Its state does not.
  EXPECT_TRUE(KeysFor(base::FilePath(std::string("/data/Engine/") + kID +
                                     "/Extension State/000003.log")));
}

TEST_F(EngineKeysTest, AProtectedPathWithNoKeyCrashes) {
  Register(kID);
  EXPECT_DEATH_IF_SUPPORTED(
      KeysFor(base::FilePath(std::string("/data/Engine/") + kOtherID + "/History")),
      "");
}

TEST_F(EngineKeysTest, DroppingForgetsEveryKeyAndStillProtects) {
  Register(kID);
  Register(kOtherID);
  EXPECT_EQ(KeyCount(), 2u);
  EXPECT_EQ(RegisteredKeys().size(), 2u);
  EXPECT_EQ(DropKeys(), 2u);
  EXPECT_EQ(KeyCount(), 0u);
  EXPECT_TRUE(RegisteredKeys().empty());
  EXPECT_DEATH_IF_SUPPORTED(
      KeysFor(base::FilePath(std::string("/data/Engine/") + kID + "/History")), "");
}

TEST_F(EngineKeysTest, EachUseHasADifferentKey) {
  Register(kID);
  const std::optional<ProfileKeys> keys =
      KeysFor(base::FilePath(std::string("/data/Engine/") + kID + "/History"));
  ASSERT_TRUE(keys);
  EXPECT_NE(keys->sqlite_pages, keys->stream_files);
  EXPECT_NE(keys->stream_files, keys->whole_files);
  EXPECT_NE(keys->stream_files, keys->stream_check);
}

}  // namespace
}  // namespace pane::encryption
