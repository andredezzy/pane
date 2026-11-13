// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/pin_guard.h"

#include <optional>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// The TPM's guard on the computer's own chip, through the Microsoft Platform
// Crypto Provider (U5). Only the right PIN is typed here: a wrong one counts
// in the chip's lockout, which Windows Hello shares, so that check is asked
// for by name (--gtest_also_run_disabled_tests), in the test VM. Without a
// TPM 2.0 each skips.

class PinGuardWinTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    guard_ = PinGuard::ForData(directory_.GetPath());
    ASSERT_TRUE(guard_);
  }

  void TearDown() override {
    if (guard_) {
      guard_->Forget(std::nullopt);
    }
  }

  // A key the chip holds for `pin`, or a skip with none.
  std::optional<Guarded> Guard(std::string_view pin) {
    base::expected<Guarded, PinGuard::Refusal> guarded = guard_->Guard(pin);
    if (!guarded.has_value()) {
      EXPECT_EQ(guarded.error(), PinGuard::Refusal::kUnavailable);
      return std::nullopt;
    }
    return std::move(*guarded);
  }

  base::ScopedTempDir directory_;
  scoped_refptr<PinGuard> guard_;
};

// The share comes back with the PIN, and not once the key is deleted.
TEST_F(PinGuardWinTest, ReleasesWithThePinUntilForgotten) {
  const std::optional<Guarded> guarded = Guard("1234");
  if (!guarded) {
    GTEST_SKIP() << "No TPM 2.0";
  }
  EXPECT_TRUE(guarded->kept.key.starts_with("Pane data key "));
  const base::expected<Key, PinGuard::Refusal> share =
      guard_->Unseal(guarded->kept, "1234");
  ASSERT_TRUE(share.has_value());
  EXPECT_EQ(*share, guarded->share);

  guard_->Forget(std::nullopt);
  EXPECT_EQ(guard_->Unseal(guarded->kept, "1234").error(),
            PinGuard::Refusal::kGone);
}

// A reseal keeps the new key and deletes the earlier; another data folder's
// keys are its own.
TEST_F(PinGuardWinTest, ForgetsAllButTheKeptKey) {
  const std::optional<Guarded> first = Guard("1234");
  if (!first) {
    GTEST_SKIP() << "No TPM 2.0";
  }
  const std::optional<Guarded> second = Guard("5678");
  ASSERT_TRUE(second);
  base::ScopedTempDir other_directory;
  ASSERT_TRUE(other_directory.CreateUniqueTempDir());
  const scoped_refptr<PinGuard> other =
      PinGuard::ForData(other_directory.GetPath());
  const base::expected<Guarded, PinGuard::Refusal> others =
      other->Guard("1234");
  ASSERT_TRUE(others.has_value());
  EXPECT_NE(others->kept.key.substr(0, 31), first->kept.key.substr(0, 31));

  guard_->Forget(second->kept.key);
  EXPECT_EQ(guard_->Unseal(first->kept, "1234").error(),
            PinGuard::Refusal::kGone);
  EXPECT_EQ(guard_->Unseal(second->kept, "5678"), second->share);
  EXPECT_EQ(other->Unseal(others->kept, "1234"), others->share);

  other->Forget(std::nullopt);
  EXPECT_EQ(other->Unseal(others->kept, "1234").error(),
            PinGuard::Refusal::kGone);
}

// A wrong PIN is the chip's refusal, and counts in its lockout: run by name,
// in the test VM.
TEST_F(PinGuardWinTest, DISABLED_AWrongPinIsRefused) {
  const std::optional<Guarded> guarded = Guard("1234");
  if (!guarded) {
    GTEST_SKIP() << "No TPM 2.0";
  }
  EXPECT_EQ(guard_->Unseal(guarded->kept, "1111").error(),
            PinGuard::Refusal::kWrongPin);
  EXPECT_EQ(guard_->Unseal(guarded->kept, "1234"), guarded->share);
}

}  // namespace
}  // namespace pane::model
