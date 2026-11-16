// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/app_lock.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_util.h"
#include "base/time/time.h"
#include "pane/model/app_lock_guard_test.h"
#include "pane/model/data_key.h"
#include "pane/model/panic_wipe_run.h"
#include "pane/model/pin_entry.h"
#include "pane/model/security.h"
#include "pane/model/stand_in_pin_guard.h"
#include "pane/model/test_waits.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

constexpr std::string_view kSecret = kGuardTestSecret;

base::span<const uint8_t> Bytes(std::string_view text) {
  return GuardTestBytes(text);
}

// Setting the PIN seals the key with a key the TPM holds, the PIN its own:
// security.json names it, the secret and the PIN alone open nothing, and a
// change makes a new one and deletes the earlier once the new seal is saved;
// removing the PIN deletes it.
TEST_F(AppLockGuardTest, ThePinIsSealedWithTheChipsKey) {
  AppLock& lock = Launch();
  ASSERT_TRUE(WaitFor([&] { return vault_->is_attached(); }));
  const Key key = *vault_->data_key();

  lock.Show(PinEntry::Mode::kSetup);
  Type("1234");
  lock.ContinueWithPin();
  Type("1234");
  // The screen sinks once the seal is on the disk (Paper 07.12).
  ASSERT_TRUE(WaitFor([&] {
    const std::optional<SecurityState> saved = Saved();
    return !lock.entry() && saved && saved->pin && saved->key &&
           saved->key->guard;
  }));
  std::optional<SecurityState> saved = Saved();
  ASSERT_EQ(guard_->keys(), std::vector<std::string>{saved->key->guard->key});
  EXPECT_EQ(OpenDataKey(*saved->key, Bytes(kSecret), "1234").error(),
            OpenError::kRefused);
  const base::expected<Key, PinGuard::Refusal> share =
      guard_->Unseal(*saved->key->guard, "1234");
  ASSERT_TRUE(share.has_value());
  EXPECT_EQ(OpenDataKey(*saved->key, Bytes(kSecret), "1234", *share), key);
  const std::string first = saved->key->guard->key;

  lock.Show(PinEntry::Mode::kChange);
  Type("1234");
  ASSERT_TRUE(WaitFor([&] {
    return lock.entry() && lock.entry()->step() == PinEntry::Step::kEnter;
  }));
  Type("5678");
  lock.ContinueWithPin();
  Type("5678");
  ASSERT_TRUE(WaitFor([&] {
    const std::optional<SecurityState> now = Saved();
    return !lock.entry() && now && now->key && now->key->guard &&
           now->key->guard->key != first && guard_->keys().size() == 1;
  }));
  saved = Saved();
  EXPECT_EQ(guard_->keys(), std::vector<std::string>{saved->key->guard->key});

  lock.Show(PinEntry::Mode::kRemove);
  Type("5678");
  ASSERT_TRUE(WaitFor([&] {
    const std::optional<SecurityState> now = Saved();
    return now && !now->pin && now->key && guard_->keys().empty();
  }));
  EXPECT_FALSE(Saved()->key->guard);
  EXPECT_EQ(OpenDataKey(*Saved()->key, Bytes(kSecret), std::nullopt), key);
}

// A new seal that didn't reach the disk keeps the earlier key (W8): the seal
// there still names it, and the earlier PIN opens it at the next launch.
// Before, the chip's earlier key went all the same, a failed save leaving
// the disk naming a key gone, and every profile with it. Since W8e the screen
// says it (Paper 07.12), and Pane ends here without an answer.
TEST_F(AppLockGuardTest, AnUnsavedSealKeepsTheEarlierKey) {
  const Key key = SaveGuardedKey("1234");
  const std::string first = Saved()->key->guard->key;
  AppLock& lock = Launch();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));

  // Neither file can be replaced.
  const std::string security = Hold("security.json");
  const std::string copy = Hold("Data.security.json");
  ChangePin("1234", "5678");
  ASSERT_TRUE(Unsaved());
  task_environment_.RunUntilIdle();
  const std::vector<std::string> both = guard_->keys();
  ASSERT_EQ(both.size(), 2u);
  EXPECT_TRUE(std::ranges::contains(both, first));

  // The disk as the failed saves left it: the earlier seal, which the
  // earlier PIN still opens.
  Quit();
  Unhold("security.json", security);
  Unhold("Data.security.json", copy);
  AppLock& again = Launch();
  ASSERT_TRUE(again.is_locked());
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !again.is_locked(); }));
  EXPECT_EQ(vault_->data_key(), key);
}

// The earlier key goes once a save of the new seal lands in both files, a
// later one when the reseal's own wrote security.json alone: here the save of
// Settings' next check of the PIN. Only the copy refusing shows nothing
// (Paper 07.12): the next launch reads security.json, which took the seal.
TEST_F(AppLockGuardTest, TheEarlierKeyGoesOnceTheNewSealIsSaved) {
  SaveGuardedKey("1234");
  const std::string first = Saved()->key->guard->key;
  AppLock& lock = Launch();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));

  Hold("Data.security.json");
  ChangePin("1234", "5678");
  ASSERT_TRUE(WaitFor([&] { return !lock.entry(); }));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(shakes_.count, 0);
  EXPECT_EQ(lock.state().pin->length, 4);
  EXPECT_NE(Saved()->key->guard->key, first);
  // One file written of two: the earlier key stays.
  ASSERT_EQ(guard_->keys().size(), 2u);

  Release("Data.security.json");
  lock.Show(PinEntry::Mode::kChange);
  Type("5678");
  ASSERT_TRUE(WaitFor([&] {
    const std::optional<SecurityState> saved = Saved("Data.security");
    return saved && saved->key && saved->key->guard &&
           saved->key->guard->key != first && guard_->keys().size() == 1;
  }));
  EXPECT_EQ(guard_->keys(), std::vector<std::string>{Saved()->key->guard->key});
  lock.Dismiss();
}

// Each flow of Settings' reseal, the disk refusing it.
struct UnsavedFlow {
  PinEntry::Mode mode;
  // Whether a PIN is set before it.
  bool had_pin;
  std::string_view lines;
  std::string_view keep;
};

class AppLockUnsavedTest : public AppLockGuardTest,
                           public testing::WithParamInterface<UnsavedFlow> {
 protected:
  // Pane open, with the PIN 1234 where the flow needs one.
  AppLock& Open() {
    if (GetParam().had_pin) {
      SaveGuardedKey("1234");
    }
    AppLock& lock = Launch();
    if (GetParam().had_pin) {
      Type("1234");
    }
    EXPECT_TRUE(WaitFor(
        [&] { return !lock.is_locked() && vault_->data_key().has_value(); }));
    return lock;
  }

  // Set up 5678, change 1234 to 5678, or remove 1234.
  void RunFlow() {
    switch (GetParam().mode) {
      case PinEntry::Mode::kSetup:
        lock_->Show(PinEntry::Mode::kSetup);
        Type("5678");
        lock_->ContinueWithPin();
        Type("5678");
        return;
      case PinEntry::Mode::kChange:
        ChangePin("1234", "5678");
        return;
      case PinEntry::Mode::kRemove:
        lock_->Show(PinEntry::Mode::kRemove);
        Type("1234");
        return;
      case PinEntry::Mode::kUnlock:
        return;
    }
  }
};

// The disk refusing the new seal is said at once, in the PIN screen, before
// it sinks (Paper 07.12): the flow's lines, the button that keeps the disk's
// seal, the dots as typed, the pad taking no digit, and nothing shaking. The
// seal the disk holds stays in force, a lock opening with it as the next
// launch does. Before, the screen sank as if saved, the new seal in force
// until Pane ended, and the next launch opened with the disk's, saying
// nothing.
TEST_P(AppLockUnsavedTest, TheDiskRefusingANewSealSaysSo) {
  AppLock& lock = Open();
  const SecurityState before = lock.state();
  const std::string security = Hold("security.json");
  const std::string copy = Hold("Data.security.json");

  RunFlow();
  ASSERT_TRUE(Unsaved());
  ASSERT_TRUE(lock.entry());
  EXPECT_EQ(lock.entry()->mode(), GetParam().mode);
  EXPECT_EQ(lock.entry()->error(), std::string(GetParam().lines));
  EXPECT_EQ(lock.entry()->KeepTitle(), GetParam().keep);
  EXPECT_EQ(lock.entry()->entered().size(), 4u);
  EXPECT_TRUE(lock.entry()->busy());
  EXPECT_FALSE(lock.is_locked());
  EXPECT_EQ(shakes_.count, 0);
  EXPECT_EQ(lock.state(), before);
  Type("9");
  lock.Backspace();
  EXPECT_EQ(lock.entry()->entered().size(), 4u);
  EXPECT_TRUE(lock.entry()->is_unsaved());

  // Esc keeps the disk's seal: the screen sinks, the seal in force as it
  // was.
  lock.Dismiss();
  EXPECT_FALSE(lock.entry());
  EXPECT_EQ(lock.state(), before);
  task_environment_.RunUntilIdle();

  // The next launch opens with what the disk held, as the lines said.
  Quit();
  Unhold("security.json", security);
  Unhold("Data.security.json", copy);
  AppLock& again = Launch();
  if (GetParam().had_pin) {
    ASSERT_TRUE(again.is_locked());
    Type("1234");
  }
  EXPECT_TRUE(WaitFor([&] { return !again.is_locked(); }));
}

INSTANTIATE_TEST_SUITE_P(
    All,
    AppLockUnsavedTest,
    testing::Values(UnsavedFlow{PinEntry::Mode::kSetup, false,
                                PinEntry::kSetupNotSaved, "Cancel"},
                    UnsavedFlow{PinEntry::Mode::kChange, true,
                                PinEntry::kChangeNotSaved, "Keep earlier PIN"},
                    UnsavedFlow{PinEntry::Mode::kRemove, true,
                                PinEntry::kRemoveNotSaved, "Keep PIN"}));

// Try again writes the seal the new PIN made again, the chip not asked: while
// the disk still refuses, the lines stay, said again, nothing shaking; once
// security.json takes it, the screen sinks, the new PIN in force, and the
// earlier key goes once both files hold the new seal.
TEST_F(AppLockGuardTest, TryAgainWritesTheSameSeal) {
  SaveGuardedKey("1234");
  const std::string first = Saved()->key->guard->key;
  AppLock& lock = Launch();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
  const std::string security = Hold("security.json");

  ChangePin("1234", "5678");
  ASSERT_TRUE(Unsaved(1));
  const std::vector<std::string> keys = guard_->keys();
  ASSERT_EQ(keys.size(), 2u);
  // The copy took it; security.json, which the next launch reads, didn't.
  EXPECT_NE(Saved("Data.security")->key->guard->key, first);

  lock.TryAgain();
  ASSERT_TRUE(Unsaved(2));
  EXPECT_EQ(shakes_.count, 0);
  EXPECT_EQ(guard_->keys(), keys);
  EXPECT_EQ(lock.entry()->error(), std::string(PinEntry::kChangeNotSaved));

  // Return tries again too.
  Unhold("security.json", security);
  lock.ContinueWithPin();
  ASSERT_TRUE(WaitFor([&] { return !lock.entry(); }));
  EXPECT_EQ(lock.state().pin->length, 4);
  ASSERT_TRUE(WaitFor([&] { return guard_->keys().size() == 1; }));
  const std::string second = guard_->keys().front();
  EXPECT_NE(second, first);
  EXPECT_EQ(Saved()->key->guard->key, second);
  EXPECT_EQ(Saved("Data.security")->key->guard->key, second);
  EXPECT_EQ(shakes_.count, 0);

  // The next launch opens with the new PIN.
  Quit();
  AppLock& again = Launch();
  Type("5678");
  EXPECT_TRUE(WaitFor([&] { return !again.is_locked(); }));
}

// Keep earlier PIN puts back the seal the disk holds and writes it again; the
// key the chip made for the new PIN goes only once both files hold the
// earlier seal: kept while security.json is still refused, forgotten at the
// next save that lands.
TEST_F(AppLockGuardTest, KeepEarlierPinForgetsTheNewKeyOnceBothHoldTheSeal) {
  SaveGuardedKey("1234");
  const std::string first = Saved()->key->guard->key;
  AppLock& lock = Launch();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
  const std::string security = Hold("security.json");

  ChangePin("1234", "5678");
  ASSERT_TRUE(Unsaved());
  ASSERT_EQ(guard_->keys().size(), 2u);

  lock.KeepSaved();
  EXPECT_FALSE(lock.entry());
  EXPECT_EQ(lock.state().key->guard->key, first);
  task_environment_.RunUntilIdle();
  // Written again, the copy back to the earlier seal; security.json still
  // refused, so the new key stays.
  EXPECT_EQ(Saved("Data.security")->key->guard->key, first);
  EXPECT_EQ(guard_->keys().size(), 2u);

  // Settings' next check of the PIN saves, both files this time.
  Unhold("security.json", security);
  lock.Show(PinEntry::Mode::kChange);
  Type("1234");
  ASSERT_TRUE(WaitFor(
      [&] { return guard_->keys() == std::vector<std::string>{first}; }));
  EXPECT_EQ(Saved()->key->guard->key, first);
  lock.Dismiss();
}

// A lock while the disk's refusal shows answers it as Esc does, as a lock
// answers any question as Cancel: the seal the disk holds stays, and the
// lock asks for the earlier PIN, which opens it, as the lines said.
TEST_F(AppLockGuardTest, ALockKeepsTheDisksSeal) {
  SaveGuardedKey("1234");
  AppLock& lock = Launch();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
  Hold("security.json");
  Hold("Data.security.json");

  lock.Show(PinEntry::Mode::kChange);
  Type("1234");
  ASSERT_TRUE(WaitFor([&] {
    return lock.entry() && lock.entry()->step() == PinEntry::Step::kEnter;
  }));
  Type("567890");
  lock.ContinueWithPin();
  Type("567890");
  ASSERT_TRUE(Unsaved());

  lock.LockNow();
  ASSERT_TRUE(lock.is_locked());
  ASSERT_TRUE(lock.entry());
  EXPECT_EQ(lock.entry()->mode(), PinEntry::Mode::kUnlock);
  EXPECT_EQ(lock.entry()->Dots(), 4);
  vault_->Lock().Run(/*every_closed=*/true);
  Type("1234");
  EXPECT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
}

// A lock that comes while a new seal is on its way to the disk waits for it,
// and is asked for again once it lands: the lock then opens with the seal in
// force, here the new PIN.
TEST_F(AppLockGuardTest, ALockWaitsForTheNewSealsWrite) {
  SaveGuardedKey("1234");
  AppLock& lock = Launch();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));

  ChangePin("1234", "567890");
  ASSERT_TRUE(lock.entry() && lock.entry()->busy());
  lock.LockNow();
  EXPECT_FALSE(lock.is_locked());
  ASSERT_TRUE(WaitFor([&] { return lock.is_locked(); }));
  EXPECT_EQ(lock_delegate_.asked_to_lock, 1);
  ASSERT_TRUE(lock.entry());
  EXPECT_EQ(lock.entry()->mode(), PinEntry::Mode::kUnlock);
  EXPECT_EQ(lock.entry()->Dots(), 6);
  Type("567890");
  EXPECT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
}

// The chip's key gone while Settings checks the PIN (Paper 07.13 B, the
// user's call: Pane's goal is to be as secure as possible): Pane locks at
// once, as Lock Pane locks it, the data key leaving memory, and the lock
// shows 07.6.1's lines for good, counting nothing. Before, those lines showed
// over the open profiles until Exit, the key still in memory.
class AppLockGuardGoneTest
    : public AppLockGuardTest,
      public testing::WithParamInterface<PinEntry::Mode> {};

TEST_P(AppLockGuardGoneTest, AKeyGoneInSettingsLocksPane) {
  SaveGuardedKey("1234");
  AppLock& lock = Launch();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
  ASSERT_TRUE(vault_->data_key());

  guard_->SetUnsealRefusal(PinGuard::Refusal::kGone);
  lock.Show(GetParam());
  Type("1234");
  ASSERT_TRUE(Unreadable());
  EXPECT_EQ(lock_delegate_.asked_to_lock, 1);
  EXPECT_EQ(lock.entry()->mode(), PinEntry::Mode::kUnlock);
  EXPECT_EQ(lock.entry()->unreadable_reason(),
            PinEntry::UnreadableReason::kGuardGone);
  EXPECT_EQ(lock.entry()->error(), words_.guard_gone);
  EXPECT_FALSE(lock.entry()->dismissable());
  EXPECT_FALSE(vault_->data_key());
  EXPECT_EQ(shakes_.count, 0);
  EXPECT_EQ(Saved()->failed_attempts, 0);
  EXPECT_EQ(guard_->wrong_pins(), 0);

  // For good: no PIN opens it, and nothing counts.
  Type("1234");
  RunFor(base::Milliseconds(100));
  EXPECT_TRUE(lock.is_locked());
  EXPECT_EQ(lock.entry()->error(), words_.guard_gone);
  EXPECT_EQ(Saved()->failed_attempts, 0);
}

INSTANTIATE_TEST_SUITE_P(All,
                         AppLockGuardGoneTest,
                         testing::Values(PinEntry::Mode::kChange,
                                         PinEntry::Mode::kRemove));

// The chip refusing while Settings checks the PIN (Paper 07.13 A): Pane
// stays open, in Settings, the dots shaking under the refusal's lines,
// nothing counted toward Settings' five or the chip's, the pad live for the
// next try, which opens once the chip answers. Before, the window turned to
// 07.6.2's lines over the open profiles until Exit.
TEST_F(AppLockGuardTest, AChipRefusingInSettingsShakesAndStays) {
  SaveGuardedKey("1234");
  AppLock& lock = Launch();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));

  guard_->SetUnsealRefusal(PinGuard::Refusal::kRefused);
  lock.Show(PinEntry::Mode::kChange);
  for (int attempt = 1; attempt <= SecurityState::kMaxAttempts + 1; ++attempt) {
    ASSERT_TRUE(Ready());
    Type("1234");
    Shook(attempt);
    EXPECT_FALSE(lock.is_locked());
    ASSERT_TRUE(lock.entry());
    EXPECT_FALSE(lock.entry()->is_unreadable());
    EXPECT_EQ(lock.entry()->mode(), PinEntry::Mode::kChange);
    EXPECT_EQ(lock.entry()->step(), PinEntry::Step::kVerify);
    EXPECT_EQ(lock.entry()->error(), words_.guard_refused);
    EXPECT_TRUE(lock.entry()->entered().empty());
  }
  EXPECT_EQ(lock_delegate_.asked_to_lock, 0);
  EXPECT_TRUE(vault_->data_key());
  EXPECT_EQ(Saved()->failed_attempts, 0);
  EXPECT_EQ(guard_->wrong_pins(), 0);

  // The next try, the chip answering: Enter new PIN.
  guard_->SetUnsealRefusal(std::nullopt);
  ASSERT_TRUE(Ready());
  Type("1234");
  ASSERT_TRUE(WaitFor([&] {
    return lock.entry() && lock.entry()->step() == PinEntry::Step::kEnter;
  }));

  // Back returns to Settings, Pane open.
  lock.Dismiss();
  EXPECT_FALSE(lock.entry());
  EXPECT_FALSE(lock.is_locked());
}

// A wrong PIN is the chip's refusal: it counts at the lock, and in the chip;
// the right one opens.
TEST_F(AppLockGuardTest, AWrongPinIsTheChipsRefusal) {
  const Key key = SaveGuardedKey("1234");
  AppLock& lock = Launch();
  ASSERT_TRUE(lock.is_locked());

  Type("1111");
  Shook(1);
  // Written behind the state.
  EXPECT_TRUE(WaitFor([&] { return Saved()->failed_attempts == 1; }));
  EXPECT_EQ(guard_->wrong_pins(), 1);

  ASSERT_TRUE(Ready());
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
  EXPECT_EQ(vault_->data_key(), key);
  EXPECT_EQ(guard_->wrong_pins(), 1);
  // Written behind the state.
  EXPECT_TRUE(WaitFor([&] { return Saved()->failed_attempts == 0; }));
}

// Another secret is told before the chip is asked: no attempt counts, at the
// lock or in the chip, and the lock shows an unreadable key.
TEST_F(AppLockGuardTest, AnotherSecretAsksTheChipNothing) {
  SaveGuardedKey("1234");
  secret_ = "b3RoZXJvdGhlcm90aGVyb3Ro";
  Launch();

  Type("1234");
  ASSERT_TRUE(Unreadable());
  EXPECT_EQ(guard_->wrong_pins(), 0);
  EXPECT_EQ(Saved()->failed_attempts, 0);
}

// The chip's lockout refuses every PIN, the right one too: nothing counts,
// the dots shake, and the lockout's words show (Paper 07.1.1) until the next
// digit, the pad live.
TEST_F(AppLockGuardTest, TheChipsLockoutCountsNothing) {
  SaveGuardedKey("1234");
  const GuardedShare kept = *Saved()->key->guard;
  for (int wrong = 0; wrong < StandInPinGuard::kLockoutAt; ++wrong) {
    ASSERT_FALSE(guard_->Unseal(kept, "0000").has_value());
  }
  AppLock& lock = Launch();

  Type("1234");
  Shook(1);
  EXPECT_TRUE(lock.is_locked());
  EXPECT_FALSE(lock.entry()->is_unreadable());
  EXPECT_EQ(lock.entry()->error(), words_.locked_out);
  EXPECT_EQ(Saved()->failed_attempts, 0);
  EXPECT_EQ(guard_->wrong_pins(), StandInPinGuard::kLockoutAt);

  Type("1");
  EXPECT_FALSE(lock.entry()->error());
  EXPECT_EQ(lock.entry()->entered(), "1");
}

// The chip cleared, or its key gone: the data behind the PIN is lost, as with
// a forgotten PIN, and the lock shows an unreadable key in its own words
// (07.6.1), counting nothing.
TEST_F(AppLockGuardTest, AKeyGoneIsAnUnreadableKey) {
  SaveGuardedKey("1234");
  guard_->Clear();
  Launch();

  Type("1234");
  ASSERT_TRUE(Unreadable());
  EXPECT_EQ(lock_->entry()->unreadable_reason(),
            PinEntry::UnreadableReason::kGuardGone);
  EXPECT_EQ(lock_->entry()->error(), words_.guard_gone);
  EXPECT_EQ(Saved()->failed_attempts, 0);
}

// No chip now for a seal that took one (the TPM off in the firmware): a state
// of its own, apart from a key gone, in its own words (07.6.2). It counts
// nothing, loses nothing, and the PIN opens the data once the chip is back.
TEST_F(AppLockGuardTest, NoChipIsApartFromAKeyGone) {
  const Key key = SaveGuardedKey("1234");
  guard_->SetNoChip(true);
  Launch();

  Type("1234");
  ASSERT_TRUE(Unreadable());
  EXPECT_EQ(lock_->entry()->unreadable_reason(),
            PinEntry::UnreadableReason::kGuardUnavailable);
  EXPECT_EQ(lock_->entry()->error(), words_.guard_unavailable);
  EXPECT_EQ(Saved()->failed_attempts, 0);
  EXPECT_EQ(guard_->keys().size(), 1u);

  lock_->RemoveObserver(&shakes_);
  lock_.reset();
  vault_.reset();
  guard_->SetNoChip(false);
  AppLock& again = Launch();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !again.is_locked(); }));
  EXPECT_EQ(vault_->data_key(), key);
}

// The system's store giving no secret says which store: on Windows DPAPI, a
// state of its own, in its own words (07.7.1).
TEST_F(AppLockGuardTest, NoSecretNamesItsStore) {
  SaveGuardedKey("1234");
  no_secret_ = true;
  Launch();

  Type("1234");
  ASSERT_TRUE(Unreadable());
  EXPECT_EQ(lock_->entry()->unreadable_reason(),
            PinEntry::UnreadableReason::kDpapiRefused);
  EXPECT_EQ(lock_->entry()->error(), words_.no_secret);
  EXPECT_EQ(Saved()->failed_attempts, 0);
}

// No TPM 2.0, for a seal that never had the chip: the PIN seals with its
// stretch alone, security.json names no chip's key, and the PIN opens it with
// no chip.
TEST_F(AppLockGuardTest, WithoutAChipTheStretchAlone) {
  guard_->SetNoChip(true);
  AppLock& lock = Launch();
  ASSERT_TRUE(WaitFor([&] { return vault_->is_attached(); }));
  const Key key = *vault_->data_key();

  lock.Show(PinEntry::Mode::kSetup);
  Type("1234");
  lock.ContinueWithPin();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] {
    const std::optional<SecurityState> saved = Saved();
    return saved && saved->pin && saved->key;
  }));
  const std::optional<SecurityState> saved = Saved();
  EXPECT_FALSE(saved->key->guard);
  EXPECT_EQ(OpenDataKey(*saved->key, Bytes(kSecret), "1234"), key);

  lock_->RemoveObserver(&shakes_);
  lock_.reset();
  vault_.reset();
  AppLock& again = Launch();
  ASSERT_TRUE(again.is_locked());
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !again.is_locked(); }));
  EXPECT_EQ(vault_->data_key(), key);
}

// A lockout makes no key either: setting the PIN then keeps the earlier seal,
// rather than one with the PIN's stretch alone, and the dots shake.
TEST_F(AppLockGuardTest, ALockoutKeepsTheEarlierSeal) {
  AppLock& lock = Launch();
  ASSERT_TRUE(WaitFor([&] { return vault_->is_attached(); }));
  const base::expected<Guarded, PinGuard::Refusal> other =
      guard_->Guard("0000");
  ASSERT_TRUE(other.has_value());
  for (int wrong = 0; wrong < StandInPinGuard::kLockoutAt; ++wrong) {
    ASSERT_FALSE(guard_->Unseal(other->kept, "1111").has_value());
  }
  const std::optional<SecurityState> before = Saved();

  lock.Show(PinEntry::Mode::kSetup);
  Type("1234");
  lock.ContinueWithPin();
  Type("1234");
  Shook(1);
  EXPECT_EQ(Saved(), before);
  EXPECT_FALSE(lock.state().pin);
  EXPECT_TRUE(lock.entry());
  // The new PIN wasn't saved, and the screen says so (Paper 07.3.1).
  EXPECT_EQ(lock.entry()->error(), words_.pin_not_saved);
}

// A chip that refuses to make a key, for a reason other than a lockout or
// its absence, seals nothing either: setting the PIN keeps the earlier seal,
// with no PIN, and the dots shake; the PIN's stretch alone is for a computer
// with no TPM 2.0.
TEST_F(AppLockGuardTest, AChipThatRefusesSealsNothing) {
  AppLock& lock = Launch();
  ASSERT_TRUE(WaitFor([&] { return vault_->is_attached(); }));
  guard_->SetGuardRefusal(PinGuard::Refusal::kRefused);
  const std::optional<SecurityState> before = Saved();

  lock.Show(PinEntry::Mode::kSetup);
  Type("1234");
  lock.ContinueWithPin();
  Type("1234");
  Shook(1);
  EXPECT_EQ(Saved(), before);
  EXPECT_FALSE(lock.state().pin);
  EXPECT_TRUE(lock.entry());
  EXPECT_EQ(lock.entry()->error(), words_.pin_not_saved);
  EXPECT_TRUE(guard_->keys().empty());
}

// A seal that had the chip keeps it: when the chip makes no key for a new
// PIN, refusing it or gone since the old PIN opened, the earlier seal stays,
// the old PIN still opens, and the dots shake. Before, a refusal sealed the
// new PIN with its stretch alone, saying nothing.
class AppLockGuardChangeTest
    : public AppLockGuardTest,
      public testing::WithParamInterface<PinGuard::Refusal> {};

TEST_P(AppLockGuardChangeTest, ASealThatHadTheChipKeepsIt) {
  const Key key = SaveGuardedKey("1234");
  AppLock& lock = Launch();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
  const std::optional<SecurityState> before = Saved();
  ASSERT_TRUE(before->key->guard);

  lock.Show(PinEntry::Mode::kChange);
  Type("1234");
  ASSERT_TRUE(WaitFor([&] {
    return lock.entry() && lock.entry()->step() == PinEntry::Step::kEnter;
  }));
  guard_->SetGuardRefusal(GetParam());
  Type("5678");
  lock.ContinueWithPin();
  Type("5678");
  Shook(1);
  EXPECT_EQ(Saved(), before);
  EXPECT_EQ(guard_->keys(), std::vector<std::string>{before->key->guard->key});
  const base::expected<Key, PinGuard::Refusal> share =
      guard_->Unseal(*before->key->guard, "1234");
  ASSERT_TRUE(share.has_value());
  EXPECT_EQ(OpenDataKey(*before->key, Bytes(kSecret), "1234", *share), key);
}

INSTANTIATE_TEST_SUITE_P(All,
                         AppLockGuardChangeTest,
                         testing::Values(PinGuard::Refusal::kRefused,
                                         PinGuard::Refusal::kNoChip));

// A seal the chip guards never wipes (the user's call, ADR 0113): wrong PINs
// count up to the last before a wipe and stay there, however many Windows
// lets through once its lockout's window frees a slot; the key stays on the
// disk, and the right PIN opens it.
TEST_F(AppLockGuardTest, AGuardedSealNeverWipes) {
  const Key key = SaveGuardedKey("1234");
  AppLock& lock = Launch();

  for (int attempt = 1; attempt <= SecurityState::kMaxAttempts + 2; ++attempt) {
    ASSERT_TRUE(Ready());
    Type("1111");
    Shook(attempt);
    EXPECT_EQ(lock.state().failed_attempts,
              std::min(attempt, SecurityState::kMaxAttempts - 1));
  }
  EXPECT_EQ(wipes_, 0);
  EXPECT_FALSE(IsWipeDue(data_));
  EXPECT_TRUE(Saved()->key);
  EXPECT_EQ(Saved()->failed_attempts, SecurityState::kMaxAttempts - 1);
  EXPECT_EQ(guard_->keys().size(), 1u);

  ASSERT_TRUE(Ready());
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
  EXPECT_EQ(vault_->data_key(), key);
}

// The PIN screen shows the attempts left only where wrong PINs wipe (Paper
// 07.1.1): on a seal the chip guards none count toward a wipe, its own count
// kept for its cap alone.
TEST_F(AppLockGuardTest, AGuardedSealCountsNoAttempts) {
  SaveGuardedKey("1234");
  AppLock& lock = Launch();
  EXPECT_EQ(lock.AttemptsTowardWipe(), std::nullopt);
  ASSERT_TRUE(Ready());
  Type("1111");
  Shook(1);
  EXPECT_EQ(lock.state().failed_attempts, 1);
  EXPECT_EQ(lock.AttemptsTowardWipe(), std::nullopt);
}

// On a seal without the chip each wrong PIN counts toward the wipe, and the
// screen shows the attempts left, as on the Mac.
TEST_F(AppLockGuardTest, ASealWithoutTheChipCountsAttempts) {
  Save(SecurityState(
      Pin{.length = 4}, 0,
      SealDataKey(MakeDataKey(), Bytes(kSecret), "1234", std::nullopt)));
  AppLock& lock = Launch();
  EXPECT_EQ(lock.AttemptsTowardWipe(), 0);
  ASSERT_TRUE(Ready());
  Type("1111");
  Shook(1);
  EXPECT_EQ(lock.AttemptsTowardWipe(), 1);
}

// A seal without the chip keeps the fifth wrong PIN's wipe, and the chip's
// keys an earlier seal left go before the save that takes the data key: a
// copy of security.json made earlier opens nothing after.
TEST_F(AppLockGuardTest, TheFifthWrongPinForgetsTheChipsKeysFirst) {
  const Key key = MakeDataKey();
  Save(SecurityState(Pin{.length = 4}, 0,
                     SealDataKey(key, Bytes(kSecret), "1234", std::nullopt)));
  ASSERT_TRUE(guard_->Guard("0000").has_value());
  std::optional<bool> key_on_disk_when_forgotten;
  guard_->SetOnForget(base::BindLambdaForTesting([&] {
    const std::optional<SecurityState> now = Saved();
    key_on_disk_when_forgotten = now && now->key;
  }));
  AppLock& lock = Launch();

  for (int attempt = 1; attempt < SecurityState::kMaxAttempts; ++attempt) {
    ASSERT_TRUE(Ready());
    Type("1111");
    Shook(attempt);
  }
  EXPECT_EQ(guard_->keys().size(), 1u);

  ASSERT_TRUE(Ready());
  Type("1111");
  ASSERT_TRUE(WaitFor([&] { return wipes_ == 1; }));
  EXPECT_TRUE(guard_->keys().empty());
  EXPECT_EQ(key_on_disk_when_forgotten, true);
  EXPECT_FALSE(Saved()->key);
  EXPECT_FALSE(Saved("Data.security")->key);
  EXPECT_TRUE(IsWipeDue(data_));
  EXPECT_TRUE(lock.is_locked());
}

// A sealed profiles.json with no key to open it is data lost: the lock stays
// shut, rather than making a key that would write over it.
TEST_F(AppLockGuardTest, DataWithNoKeyStaysShut) {
  ASSERT_TRUE(base::WriteFile(data_.AppendASCII("profiles.json"), "{}"));
  AppLock& lock = Launch();
  EXPECT_TRUE(lock.is_locked());
  ASSERT_TRUE(lock.entry());
  EXPECT_TRUE(lock.entry()->is_unreadable());
  RunFor(base::Milliseconds(100));
  EXPECT_FALSE(vault_->data_key());
}

}  // namespace
}  // namespace pane::model
