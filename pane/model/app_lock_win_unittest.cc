// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/app_lock.h"

#include <windows.h>

#include <ncrypt.h>
#include <winerror.h>

#include <string>

#include "pane/model/app_lock_guard_test.h"
#include "pane/model/pin_entry.h"
#include "pane/model/pin_guard.h"
#include "pane/model/pin_guard_win.h"
#include "pane/model/security.h"
#include "pane/model/test_waits.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// What Settings' Change and Remove PIN do with each answer Windows' provider
// gives for the current PIN, as the test VM's chip gave them (W8,
// RefusalAtOpen and RefusalAtDecrypt), on the stand-in TPM.
struct ChipAnswer {
  const char* name;
  PinGuard::Refusal refusal;
  PinEntry::Mode mode;
};

// The key lost: deleted (NTE_BAD_KEYSET at its open, as for every key of a
// chip cleared) or its file damaged (TPM_20_E_INTEGRITY at the decrypt).
// Pane locks at once in 07.6.1's words, the data key leaving memory (Paper
// 07.13 B).
class AppLockKeyLostTest : public AppLockGuardTest,
                           public testing::WithParamInterface<ChipAnswer> {};

TEST_P(AppLockKeyLostTest, LocksPane) {
  SaveGuardedKey("1234");
  AppLock& lock = Launch();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));

  guard_->SetUnsealRefusal(GetParam().refusal);
  lock.Show(GetParam().mode);
  Type("1234");
  ASSERT_TRUE(Unreadable());
  EXPECT_EQ(lock_delegate_.asked_to_lock, 1);
  EXPECT_EQ(lock.entry()->unreadable_reason(),
            PinEntry::UnreadableReason::kGuardGone);
  EXPECT_EQ(lock.entry()->error(), words_.guard_gone);
  EXPECT_FALSE(vault_->data_key());
  EXPECT_EQ(Saved()->failed_attempts, 0);
}

INSTANTIATE_TEST_SUITE_P(
    All,
    AppLockKeyLostTest,
    testing::Values(ChipAnswer{"ChangeBadKeyset",
                               RefusalAtOpen(NTE_BAD_KEYSET, true),
                               PinEntry::Mode::kChange},
                    ChipAnswer{"ChangeIntegrity",
                               RefusalAtDecrypt(TPM_20_E_INTEGRITY, true),
                               PinEntry::Mode::kChange},
                    ChipAnswer{"RemoveBadKeyset",
                               RefusalAtOpen(NTE_BAD_KEYSET, true),
                               PinEntry::Mode::kRemove},
                    ChipAnswer{"RemoveIntegrity",
                               RefusalAtDecrypt(TPM_20_E_INTEGRITY, true),
                               PinEntry::Mode::kRemove}),
    [](const testing::TestParamInfo<ChipAnswer>& info) {
      return std::string(info.param.name);
    });

// Any other answer: the chip refusing for now. Pane stays open, in
// Settings, the dots shaking under 07.4.2's lines, nothing counted, the data
// key kept (Paper 07.13 A).
class AppLockChipRefusesTest : public AppLockGuardTest,
                               public testing::WithParamInterface<ChipAnswer> {
};

TEST_P(AppLockChipRefusesTest, StaysInSettings) {
  SaveGuardedKey("1234");
  AppLock& lock = Launch();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));

  guard_->SetUnsealRefusal(GetParam().refusal);
  lock.Show(GetParam().mode);
  Type("1234");
  Shook(1);
  EXPECT_FALSE(lock.is_locked());
  EXPECT_EQ(lock_delegate_.asked_to_lock, 0);
  ASSERT_TRUE(lock.entry());
  EXPECT_EQ(lock.entry()->mode(), GetParam().mode);
  EXPECT_EQ(lock.entry()->error(), words_.guard_refused);
  EXPECT_TRUE(vault_->data_key());
  EXPECT_EQ(Saved()->failed_attempts, 0);
  EXPECT_EQ(guard_->wrong_pins(), 0);
}

INSTANTIATE_TEST_SUITE_P(
    All,
    AppLockChipRefusesTest,
    testing::Values(ChipAnswer{"ChangeNotReady",
                               RefusalAtOpen(NTE_DEVICE_NOT_READY, true),
                               PinEntry::Mode::kChange},
                    ChipAnswer{"ChangeInternalError",
                               RefusalAtDecrypt(NTE_INTERNAL_ERROR, true),
                               PinEntry::Mode::kChange},
                    ChipAnswer{"RemoveNotReady",
                               RefusalAtOpen(NTE_DEVICE_NOT_READY, true),
                               PinEntry::Mode::kRemove},
                    ChipAnswer{"RemoveInternalError",
                               RefusalAtDecrypt(NTE_INTERNAL_ERROR, true),
                               PinEntry::Mode::kRemove}),
    [](const testing::TestParamInfo<ChipAnswer>& info) {
      return std::string(info.param.name);
    });

}  // namespace
}  // namespace pane::model
