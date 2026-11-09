// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/pin_hash.h"

#include <algorithm>
#include <array>
#include <string>

#include "pane/model/mac/bcrypt/bcrypt.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's PinHashTests, in its SecurityTests.

constexpr char kHash[] =
    "$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO";

// The Electron app's SALT_ROUNDS.
constexpr int kElectronRounds = 10;

// A $2b$ hash at the Electron app's cost, as PinHash.make made one, through
// the C itself: Pane makes no hash any more.
std::string MakeHash(const char* pin) {
  std::array<char, 61> hash = {};
  if (bcrypt_newhash(pin, kElectronRounds, hash.data(), hash.size()) != 0) {
    return std::string();
  }
  return std::string(hash.begin(), std::ranges::find(hash, '\0'));
}

// Made by the Electron app's bcryptjs 3.0.3 (hashSync).
// Verifies the Electron app's bcryptjs hashes.
TEST(PinHashTest, ElectronHashes) {
  EXPECT_TRUE(VerifyPinHash("1234", kHash));
  EXPECT_TRUE(VerifyPinHash(
      "908172",
      "$2b$10$L3aErfv3P/fHFyJoj1nlbOlhXNE/XlPmAx40ZQ5PfES6lQ53p2mXC"));
  EXPECT_TRUE(VerifyPinHash(
      "0000", "$2b$04$FDOi3iSOShZpLJbeU3jh0emN7Ma0VvZ./v68GcSUz/UqOZApKkSKK"));
  EXPECT_FALSE(VerifyPinHash("1235", kHash));
  EXPECT_FALSE(VerifyPinHash("", kHash));
}

// bcryptjs 2 wrote $2a$; for a PIN, 2a and 2b hash alike (they differ only
// past 255 bytes).
// Verifies an older $2a$ hash.
TEST(PinHashTest, TwoA) {
  EXPECT_TRUE(VerifyPinHash(
      "1234", "$2a$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO"));
}

// Rejects a malformed hash instead of crashing.
TEST(PinHashTest, Malformed) {
  EXPECT_FALSE(VerifyPinHash("1234", ""));
  EXPECT_FALSE(VerifyPinHash("1234", "not a hash"));
  EXPECT_FALSE(VerifyPinHash("1234", "$2b$10$short"));
}

// Makes salted $2b$ hashes at the Electron app's cost.
TEST(PinHashTest, Makes) {
  const std::string first = MakeHash("4321");
  const std::string second = MakeHash("4321");

  EXPECT_TRUE(first.starts_with("$2b$10$"));
  EXPECT_EQ(first.size(), 60u);
  EXPECT_NE(first, second);
  EXPECT_TRUE(VerifyPinHash("4321", first));
  EXPECT_FALSE(VerifyPinHash("4322", first));
}

}  // namespace
}  // namespace pane::model
