// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/security.h"

#include <string>
#include <string_view>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_util.h"
#include "pane/model/persisted_file.h"
#include "pane/model/pin_entry.h"
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

// Ported from PaneKit's PinEntryTests.

using Kind = PinEntry::Action::Kind;

PinEntry::Action Act(Kind kind, std::string pin = "") {
  return {.kind = kind, .pin = std::move(pin)};
}

// Types each digit, its action dropped.
void Type(PinEntry& entry, std::string_view digits) {
  for (char digit : digits) {
    entry.Digit(digit);
  }
}

// Unlock submits on the stored length, and unlocks on success.
TEST(PinEntryTest, Unlock) {
  PinEntry entry(PinEntry::Mode::kUnlock, 4);

  EXPECT_EQ(entry.Title(), std::nullopt);
  EXPECT_FALSE(entry.dismissable());
  EXPECT_EQ(entry.Dots(), 4);
  EXPECT_EQ(entry.Digit('1'), Act(Kind::kNone));
  EXPECT_EQ(entry.Digit('2'), Act(Kind::kNone));
  EXPECT_EQ(entry.Digit('3'), Act(Kind::kNone));
  EXPECT_EQ(entry.Digit('4'), Act(Kind::kVerify, "1234"));
  EXPECT_EQ(entry.Digit('5'), Act(Kind::kNone));
  EXPECT_EQ(entry.Verified(true, 5), Act(Kind::kUnlock));
}

// A wrong PIN shakes and starts over; the last one wipes.
TEST(PinEntryTest, WrongPin) {
  PinEntry entry(PinEntry::Mode::kUnlock, 4);

  Type(entry, "0000");

  EXPECT_EQ(entry.Verified(false, 2), Act(Kind::kShake));
  entry.Shook();
  EXPECT_TRUE(entry.entered().empty());
  EXPECT_FALSE(entry.busy());

  Type(entry, "0000");

  EXPECT_EQ(entry.Verified(false, 0), Act(Kind::kWipe));
}

// Setup asks for at least four digits, then the same again.
TEST(PinEntryTest, Setup) {
  PinEntry entry(PinEntry::Mode::kSetup, 0);

  EXPECT_EQ(entry.Title(), "Enter new PIN");
  EXPECT_EQ(entry.Dots(), 4);

  for (char digit : std::string_view("123")) {
    EXPECT_EQ(entry.Digit(digit), Act(Kind::kNone));
  }

  EXPECT_FALSE(entry.CanContinue());
  entry.ContinueWithPin();
  EXPECT_EQ(entry.step(), PinEntry::Step::kEnter);

  entry.Digit('4');
  entry.Digit('5');
  EXPECT_EQ(entry.Dots(), 5);
  EXPECT_TRUE(entry.CanContinue());
  entry.ContinueWithPin();

  EXPECT_EQ(entry.step(), PinEntry::Step::kConfirm);
  EXPECT_EQ(entry.Title(), "Confirm PIN");
  EXPECT_EQ(entry.Dots(), 5);

  Type(entry, "1234");

  EXPECT_EQ(entry.Digit('6'), Act(Kind::kShake));
  EXPECT_EQ(entry.error(), "PINs don't match");
  entry.Shook();

  Type(entry, "1234");

  EXPECT_EQ(entry.Digit('5'), Act(Kind::kSetPin, "12345"));
}

// Change verifies the current PIN, then takes a new one.
TEST(PinEntryTest, Change) {
  PinEntry entry(PinEntry::Mode::kChange, 4);

  EXPECT_EQ(entry.Title(), "Enter current PIN");

  Type(entry, "123");

  EXPECT_EQ(entry.Digit('4'), Act(Kind::kVerify, "1234"));
  EXPECT_EQ(entry.Verified(true, 5), Act(Kind::kNone));
  EXPECT_EQ(entry.step(), PinEntry::Step::kEnter);
  EXPECT_TRUE(entry.entered().empty());

  Type(entry, "9876");

  entry.ContinueWithPin();

  Type(entry, "987");

  EXPECT_EQ(entry.Digit('6'), Act(Kind::kSetPin, "9876"));
}

// Remove verifies the current PIN; a wrong one only shakes.
TEST(PinEntryTest, Remove) {
  PinEntry entry(PinEntry::Mode::kRemove, 4);

  Type(entry, "1111");

  EXPECT_EQ(entry.Verified(false, 0), Act(Kind::kShake));
  entry.Shook();

  Type(entry, "1234");

  EXPECT_EQ(entry.Verified(true, 5), Act(Kind::kRemovePin, "1234"));
}

// An unreadable lock says why and takes no keys.
TEST(PinEntryTest, Unreadable) {
  PinEntry entry = PinEntry::Unreadable("Can't read the PIN");

  EXPECT_TRUE(entry.mode() == PinEntry::Mode::kUnlock && !entry.dismissable());
  EXPECT_EQ(entry.error(), "Can't read the PIN");
  EXPECT_EQ(entry.Digit('1'), Act(Kind::kNone));
  EXPECT_TRUE(entry.entered().empty());
}

// Backspace and non-digits.
TEST(PinEntryTest, Editing) {
  PinEntry entry(PinEntry::Mode::kSetup, 0);

  EXPECT_EQ(entry.Digit('a'), Act(Kind::kNone));
  entry.Digit('1');
  entry.Digit('2');
  entry.Backspace();
  EXPECT_EQ(entry.entered(), "1");
  entry.Backspace();
  entry.Backspace();
  EXPECT_TRUE(entry.entered().empty());
}

}  // namespace
}  // namespace pane::model
