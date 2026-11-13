// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/engine_keys.h"

#include <initializer_list>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/gtest_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::encryption {
namespace {

constexpr char kID[] = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02";
constexpr char kOtherID[] = "aaaaaaaa-8b3e-4c7d-9a2f-1e5b7c3d9f02";

// `base` with each of `parts` appended, '/' within one making folders.
base::FilePath Under(const base::FilePath& base,
                     std::initializer_list<std::string> parts) {
  base::FilePath path = base;
  for (const std::string& part : parts) {
    path = path.Append(base::FilePath::FromUTF8Unsafe(part));
  }
  return path;
}

// The shapes a Mac's roots have, as absolute paths on any OS: the engine's
// user data dir, its volume, and its caches' folder.
class EngineKeysTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_.CreateUniqueTempDir());
    base_ = temp_.GetPath();
    engine_ = Under(base_, {"data", "Engine"});
    caches_ = Under(base_, {"Caches", "pane", "Engine"});
    ResetForTesting();
    AddRoots({engine_, engine_.AppendASCII("Volume"), caches_});
  }
  void TearDown() override { ResetForTesting(); }

  void Register(const std::string& id) {
    Key key;
    key.fill(7);
    RegisterKey(id, key);
  }

  base::ScopedTempDir temp_;
  base::FilePath base_;
  base::FilePath engine_;
  base::FilePath caches_;
};

TEST_F(EngineKeysTest, EveryFormOfAProfilesPathIsItsKeys) {
  Register(kID);

  for (const base::FilePath& path :
       {Under(engine_, {kID, "History"}),
        Under(engine_, {"Volume", kID, "Profile", "History"}),
        Under(engine_, {"Volume", kID, "Cache", "Cache_Data", "index"}),
        Under(caches_, {kID, "Cache", "Cache_Data", "index"})}) {
    EXPECT_TRUE(KeysFor(path)) << path;
  }
}

TEST_F(EngineKeysTest, WhatIsNotAProfilesFileStaysPlain) {
  Register(kID);

  for (const base::FilePath& path :
       {Under(engine_, {"Local State"}), Under(engine_, {"Default", "History"}),
        Under(engine_, {"Safe Browsing", "Bloom"}),
        Under(base_, {"data", "settings.json"}),
        Under(base_, {"elsewhere", "History"}),
        Under(engine_, {"Volume", ".fseventsd", "0001"})}) {
    EXPECT_FALSE(KeysFor(path)) << path;
  }
}

TEST_F(EngineKeysTest, ExtensionsCodeStaysPlain) {
  Register(kID);
  EXPECT_FALSE(
      KeysFor(Under(engine_, {kID, "Extensions", "abc", "1.0", "a.json"})));
  EXPECT_FALSE(KeysFor(
      Under(engine_, {"Volume", kID, "Profile", "Extensions", "abc", "a.js"})));
  // Its state does not.
  EXPECT_TRUE(KeysFor(Under(engine_, {kID, "Extension State", "000003.log"})));
}

TEST_F(EngineKeysTest, AProtectedPathWithNoKeyCrashes) {
  Register(kID);
  EXPECT_DEATH_IF_SUPPORTED(KeysFor(Under(engine_, {kOtherID, "History"})), "");
}

// A registered profile's folder reached in a form no root has, as a short
// name, a link, or a junction gives it: refused, never written plain, even
// once the keys are gone.
TEST_F(EngineKeysTest, AProfilesFileInAFormNoRootHasCrashes) {
  Register(kID);
  const base::FilePath elsewhere =
      Under(base_, {"elsewhere", "Engine", kID, "History"});
  EXPECT_DEATH_IF_SUPPORTED(KeysFor(elsewhere), "");
  DropKeys();
  EXPECT_DEATH_IF_SUPPORTED(KeysFor(elsewhere), "");
}

// Its extensions' public code stays plain in any form, as under a root.
TEST_F(EngineKeysTest, ExtensionsCodeInAFormNoRootHasStaysPlain) {
  Register(kID);
  EXPECT_FALSE(KeysFor(
      Under(base_, {"elsewhere", kID, "Extensions", "abc", "manifest.json"})));
}

TEST_F(EngineKeysTest, DroppingForgetsEveryKeyAndStillProtects) {
  Register(kID);
  Register(kOtherID);
  EXPECT_EQ(KeyCount(), 2u);
  EXPECT_EQ(RegisteredKeys().size(), 2u);
  EXPECT_EQ(DropKeys(), 2u);
  EXPECT_EQ(KeyCount(), 0u);
  EXPECT_TRUE(RegisteredKeys().empty());
  EXPECT_DEATH_IF_SUPPORTED(KeysFor(Under(engine_, {kID, "History"})), "");
}

TEST_F(EngineKeysTest, EachUseHasADifferentKey) {
  Register(kID);
  const std::optional<ProfileKeys> keys =
      KeysFor(Under(engine_, {kID, "History"}));
  ASSERT_TRUE(keys);
  EXPECT_NE(keys->sqlite_pages, keys->stream_files);
  EXPECT_NE(keys->stream_files, keys->whole_files);
  EXPECT_NE(keys->stream_files, keys->stream_check);
}

// The roots a user data dir gives reach its profiles as given, and as the file
// system resolves it.
TEST_F(EngineKeysTest, TheRootsOfAUserDataDirReachItsProfiles) {
  ResetForTesting();
  const base::FilePath user_data_dir = Under(base_, {"data", "Engine"});
  ASSERT_TRUE(base::CreateDirectory(user_data_dir));
  const std::vector<base::FilePath> roots = RootsOf(user_data_dir);
  ASSERT_FALSE(roots.empty());
  EXPECT_EQ(roots.front(), user_data_dir);
  AddRoots(roots);
  Register(kID);

  for (const base::FilePath& root : roots) {
    EXPECT_TRUE(KeysFor(Under(root, {kID, "History"}))) << root;
  }
}

}  // namespace
}  // namespace pane::encryption
