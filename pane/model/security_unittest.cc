// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/security.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_util.h"
#include "pane/model/persisted_file.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's SecurityStateTests, case by case.

constexpr char kHash[] =
    "$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO";

// Reads and writes security.json as the Electron app did.
TEST(SecurityStateTest, ElectronFile) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const std::string electron =
      std::string(R"({"state":{"pin":{"hash":")") + kHash +
      R"(","length":4},"failedAttempts":2},"version":0})";
  ASSERT_TRUE(base::WriteFile(directory.GetPath().AppendASCII("security.json"),
                              electron));

  const PersistedFile file(directory.GetPath(), "security");
  const std::optional<SecurityState> state =
      SecurityState::FromValue(file.Load().value().value());
  ASSERT_TRUE(state);

  EXPECT_EQ(state->pin->length, 4);
  EXPECT_EQ(state->failed_attempts, 2);
  EXPECT_EQ(state->remaining_attempts(), 3);

  ASSERT_TRUE(file.Save(SecurityState().ToValue()));
  std::string written;
  ASSERT_TRUE(base::ReadFileToString(file.path(), &written));

  EXPECT_EQ(written,
            R"({"state":{"failedAttempts":0,"pin":null},"version":0})");
}

// A damaged file gives back its PIN when it still holds one.
TEST(SecurityStateTest, Recover) {
  // Cut short, and in an order no Pane writes.
  const std::string truncated =
      std::string(
          R"({"version":0,"state":{"failedAttempts":3,"pin":{"length":6,"hash":")") +
      kHash + "\"}";

  EXPECT_EQ(SecurityState::Recover(truncated),
            SecurityState(Pin{.hash = kHash, .length = 6}, 3, std::nullopt));

  EXPECT_EQ(SecurityState::Recover(std::string(R"({"state":{"pin":{"hash":")") +
                                   kHash),
            std::nullopt);
  EXPECT_EQ(SecurityState::Recover(std::string("\0\0garbage", 9)),
            std::nullopt);
  EXPECT_EQ(SecurityState::Recover(
                R"({"state":{"pin":{"hash":"$2b$10$short","length":4}}})"),
            std::nullopt);
}

// A damaged file from 0.13.1 gives back its PIN, its slashes escaped.
TEST(SecurityStateTest, RecoverEscapedSlashes) {
  // bcrypt's alphabet holds "/", which 0.13.1's JSONEncoder wrote as "\/".
  std::string hash = "$2b$10$";
  for (int i = 0; i < 26; ++i) {
    hash += "a/";
  }
  hash += "b";
  std::string escaped = hash;
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "/", "\\/");

  const std::string truncated =
      R"({"state":{"failedAttempts":2,"pin":{"length":4,"hash":")" + escaped +
      "\"";
  EXPECT_EQ(SecurityState::Recover(truncated),
            SecurityState(Pin{.hash = hash, .length = 4}, 2, std::nullopt));
}

}  // namespace
}  // namespace pane::model
