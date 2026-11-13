// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/engine_keys.h"

#include <windows.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_util.h"
#include "base/test/file_path_reparse_point_win.h"
#include "base/test/gtest_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::encryption {
namespace {

constexpr char kID[] = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02";
constexpr wchar_t kUpperID[] = L"6F9D2C1A-8B3E-4C7D-9A2F-1E5B7C3D9F02";

// A user data dir under a folder whose long name NTFS also names short, where
// the volume makes short names.
class EngineKeysWinTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_.CreateUniqueTempDir());
    ResetForTesting();
    base::FilePath temp;
    ASSERT_TRUE(base::NormalizeFilePath(temp_.GetPath(), &temp));
    parent_ = temp.Append(L"Pane Data Folder");
    user_data_dir_ = parent_.Append(L"Engine");
    ASSERT_TRUE(base::CreateDirectory(user_data_dir_));
  }

  void TearDown() override { ResetForTesting(); }

  void Register() {
    Key key;
    key.fill(7);
    RegisterKey(kID, key);
  }

  base::FilePath History(const base::FilePath& root) {
    return root.AppendASCII(kID).Append(L"History");
  }

  base::ScopedTempDir temp_;
  base::FilePath parent_;
  base::FilePath user_data_dir_;
};

// A user data dir given in another case: its roots hold the case on disk.
TEST_F(EngineKeysWinTest, TheRootsHoldTheCaseOnDisk) {
  const base::FilePath given(base::ToUpperASCII(user_data_dir_.value()));
  ASSERT_NE(given, user_data_dir_);
  EXPECT_EQ(RootsOf(given),
            (std::vector<base::FilePath>{given, user_data_dir_}));
}

// Through a junction: its roots hold the folder it leads to.
TEST_F(EngineKeysWinTest, TheRootsFollowAJunction) {
  const base::FilePath junction = temp_.GetPath().Append(L"Junction");
  ASSERT_TRUE(base::CreateDirectory(junction));
  std::optional<base::test::FilePathReparsePoint> point =
      base::test::FilePathReparsePoint::Create(junction, parent_);
  ASSERT_TRUE(point);

  const base::FilePath given = junction.Append(L"Engine");
  const std::vector<base::FilePath> roots = RootsOf(given);
  EXPECT_EQ(roots, (std::vector<base::FilePath>{given, user_data_dir_}));

  AddRoots(roots);
  Register();
  EXPECT_TRUE(KeysFor(History(given)));
  EXPECT_TRUE(KeysFor(History(user_data_dir_)));
}

// By its short name: its roots hold the long one.
TEST_F(EngineKeysWinTest, TheRootsMakeAShortNameLong) {
  wchar_t buffer[MAX_PATH];
  const DWORD length =
      ::GetShortPathNameW(user_data_dir_.value().c_str(), buffer, MAX_PATH);
  ASSERT_GT(length, 0u);
  ASSERT_LT(length, static_cast<DWORD>(MAX_PATH));
  const base::FilePath given(std::wstring(buffer, length));

  if (given == user_data_dir_) {
    GTEST_SKIP() << "this volume makes no short names";
  }

  const std::vector<base::FilePath> roots = RootsOf(given);
  EXPECT_EQ(roots, (std::vector<base::FilePath>{given, user_data_dir_}));

  AddRoots(roots);
  Register();
  EXPECT_TRUE(KeysFor(History(given)));
  EXPECT_TRUE(KeysFor(History(user_data_dir_)));
}

// Names compare without case, as NTFS compares them, and '/' separates too:
// one file, one key, its extensions' code plain in any case.
TEST_F(EngineKeysWinTest, AnotherCaseOrSeparatorIsTheSameFile) {
  AddRoots({user_data_dir_});
  Register();

  const base::FilePath lower(base::ToLowerASCII(user_data_dir_.value()));
  EXPECT_TRUE(KeysFor(lower.Append(kUpperID).Append(L"History")));

  std::wstring slashes = History(user_data_dir_).value();
  std::replace(slashes.begin(), slashes.end(), L'\\', L'/');
  EXPECT_TRUE(KeysFor(base::FilePath(slashes)));

  EXPECT_FALSE(KeysFor(
      user_data_dir_.AppendASCII(kID).Append(L"extensions\\abc\\a.js")));
  EXPECT_TRUE(SameComponent(L"Engine", L"ENGINE"));
}

// A profile's folder reached by a \\?\ path, or a share, is in a form no root
// has: refused, never written plain.
TEST_F(EngineKeysWinTest, AnExtendedOrSharedPathIsRefused) {
  AddRoots({user_data_dir_});
  Register();
  const std::wstring path = History(user_data_dir_).value();
  EXPECT_DEATH_IF_SUPPORTED(KeysFor(base::FilePath(L"\\\\?\\" + path)), "");
  EXPECT_DEATH_IF_SUPPORTED(
      KeysFor(base::FilePath(L"\\\\localhost\\" + path.substr(0, 1) + L"$" +
                             path.substr(2))),
      "");
}

}  // namespace
}  // namespace pane::encryption
