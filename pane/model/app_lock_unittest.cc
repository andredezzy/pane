// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/app_lock.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "pane/model/data_key.h"
#include "pane/model/data_lock.h"
#include "pane/model/data_vault.h"
#include "pane/model/folder_place.h"
#include "pane/model/panic_wipe_run.h"
#include "pane/model/persisted_file.h"
#include "pane/model/pin_entry.h"
#include "pane/model/security.h"
#include "pane/model/stand_in_pin_guard.h"
#include "pane/model/test_waits.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// The lock with the PIN's guard (U5), on every system, with a TPM in memory
// (StandInPinGuard) and the engine's secret given in: what the guard adds to
// the seal, and what each of the chip's answers does. The Mac's own rules,
// on its volume and Keychain, are mac/app_lock_unittest.cc's.

constexpr std::string_view kSecret = "c2VjcmV0c2VjcmV0c2VjcmV0";

base::span<const uint8_t> Bytes(std::string_view text) {
  return base::as_byte_span(text);
}

class VaultDelegate : public DataVault::Delegate {
 public:
  void DataKeyDidOpen(base::OnceClosure loaded) override {
    std::move(loaded).Run();
  }
  std::vector<std::string> ProfileIds() override { return {}; }
  void AccessDidOpen() override {}
  void VolumeWontOpen() override {}
  void QuitToCloseVolume() override {}
  void VolumeDidDetachAtLock() override {}
};

class LockDelegate : public AppLock::Delegate {
 public:
  void DidUnlock() override { ++unlocks; }
  void DidExhaustSettingsAttempts() override { ++exhausted; }

  int unlocks = 0;
  int exhausted = 0;
};

class Shakes : public AppLock::Observer {
 public:
  void OnPinShake() override { ++count; }

  int count = 0;
};

class AppLockGuardTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    data_ = directory_.GetPath().AppendASCII("data");
    ASSERT_TRUE(base::CreateDirectory(data_));
    guard_ = base::MakeRefCounted<StandInPinGuard>();
  }

  void TearDown() override {
    task_environment_.RunUntilIdle();
    if (lock_) {
      lock_->RemoveObserver(&shakes_);
    }
    lock_.reset();
    vault_.reset();
  }

  // The vault and the lock, as Windows' launch makes them: the engine's
  // folder, the secret given in (`secret_`), and the TPM in memory.
  AppLock& Launch() {
    vault_ = std::make_unique<DataVault>(
        data_, base::MakeRefCounted<FolderPlace>(data_, DataLock::Take(data_)),
        &vault_delegate_);
    AppLock::System system;
    system.secret = base::BindLambdaForTesting(
        [&](bool makes_one) -> std::optional<std::vector<uint8_t>> {
          if (no_secret_) {
            return std::nullopt;
          }
          return base::ToVector(Bytes(secret_));
        });
    system.guard = guard_;
    system.no_secret = PinEntry::UnreadableReason::kDpapiRefused;
    lock_ = std::make_unique<AppLock>(
        data_,
        AppLock::Load(data_,
                      base::PathExists(data_.AppendASCII("profiles.json"))),
        *vault_, system, &lock_delegate_);
    lock_->SetWipeForTesting(base::BindLambdaForTesting([&] { ++wipes_; }));
    lock_->AddObserver(&shakes_);
    lock_->Start();
    return *lock_;
  }

  void Save(const SecurityState& state) {
    for (std::string_view name : {"security", "Data.security"}) {
      ASSERT_TRUE(PersistedFile(data_, name).Save(state.ToValue()));
    }
  }

  std::optional<SecurityState> Saved(std::string_view name = "security") const {
    const base::expected<std::optional<base::Value>, FileError> state =
        PersistedFile(data_, name).Load();
    if (!state.has_value() || !*state) {
      return std::nullopt;
    }
    return SecurityState::FromValue(**state);
  }

  // A key sealed under kSecret with `pin`, the stand-in TPM guarding it, in
  // both files.
  Key SaveGuardedKey(std::string_view pin) {
    const Key key = MakeDataKey();
    const base::expected<Guarded, PinGuard::Refusal> guarded =
        guard_->Guard(pin);
    EXPECT_TRUE(guarded.has_value());
    Save(SecurityState(Pin{.length = static_cast<int>(pin.size())}, 0,
                       SealDataKey(key, Bytes(kSecret), pin, *guarded)));
    return key;
  }

  void Type(std::string_view digits) {
    for (char digit : digits) {
      lock_->Digit(digit);
    }
  }

  [[nodiscard]] bool Ready() {
    return WaitFor([&] { return lock_->entry() && !lock_->entry()->busy(); });
  }

  void Shook(int count) {
    ASSERT_TRUE(WaitFor([&] { return shakes_.count == count; }));
    task_environment_.AdvanceClock(base::Milliseconds(600));
    base::RunLoop().RunUntilIdle();
  }

  [[nodiscard]] bool Unreadable() {
    return WaitFor([&] {
      return lock_->is_locked() && lock_->entry() &&
             lock_->entry()->is_unreadable();
    });
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir directory_;
  base::FilePath data_;
  std::string secret_{kSecret};
  // DPAPI gives no secret.
  bool no_secret_ = false;
  scoped_refptr<StandInPinGuard> guard_;
  VaultDelegate vault_delegate_;
  LockDelegate lock_delegate_;
  Shakes shakes_;
  int wipes_ = 0;
  std::unique_ptr<DataVault> vault_;
  std::unique_ptr<AppLock> lock_;
};

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
  ASSERT_TRUE(WaitFor([&] {
    const std::optional<SecurityState> saved = Saved();
    return saved && saved->pin && saved->key && saved->key->guard;
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
    return now && now->key && now->key->guard &&
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

// A wrong PIN is the chip's refusal: it counts at the lock, and in the chip;
// the right one opens.
TEST_F(AppLockGuardTest, AWrongPinIsTheChipsRefusal) {
  const Key key = SaveGuardedKey("1234");
  AppLock& lock = Launch();
  ASSERT_TRUE(lock.is_locked());

  Type("1111");
  Shook(1);
  EXPECT_EQ(Saved()->failed_attempts, 1);
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
// and the dots shake.
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
  EXPECT_EQ(Saved()->failed_attempts, 0);
  EXPECT_EQ(guard_->wrong_pins(), StandInPinGuard::kLockoutAt);
}

// The chip cleared, or its key gone: the data behind the PIN is lost, as with
// a forgotten PIN, and the lock shows an unreadable key (07.6), counting
// nothing.
TEST_F(AppLockGuardTest, AKeyGoneIsAnUnreadableKey) {
  SaveGuardedKey("1234");
  guard_->Clear();
  Launch();

  Type("1234");
  ASSERT_TRUE(Unreadable());
  EXPECT_EQ(lock_->entry()->unreadable_reason(),
            PinEntry::UnreadableReason::kGuardGone);
  EXPECT_EQ(lock_->entry()->error(), AppLock::kKeyUnreadable);
  EXPECT_EQ(Saved()->failed_attempts, 0);
}

// No chip now for a seal that took one (the TPM off in the firmware): a state
// of its own, apart from a key gone, though its words are 07.6's until
// Paper's 07.6.2. It counts nothing, loses nothing, and the PIN opens the
// data once the chip is back.
TEST_F(AppLockGuardTest, NoChipIsApartFromAKeyGone) {
  const Key key = SaveGuardedKey("1234");
  guard_->SetNoChip(true);
  Launch();

  Type("1234");
  ASSERT_TRUE(Unreadable());
  EXPECT_EQ(lock_->entry()->unreadable_reason(),
            PinEntry::UnreadableReason::kGuardUnavailable);
  EXPECT_EQ(lock_->entry()->error(), AppLock::kKeyUnreadable);
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
// state of its own, though its words are the Keychain's 07.7 until Paper's
// 07.7.1.
TEST_F(AppLockGuardTest, NoSecretNamesItsStore) {
  SaveGuardedKey("1234");
  no_secret_ = true;
  Launch();

  Type("1234");
  ASSERT_TRUE(Unreadable());
  EXPECT_EQ(lock_->entry()->unreadable_reason(),
            PinEntry::UnreadableReason::kDpapiRefused);
  EXPECT_EQ(lock_->entry()->error(), AppLock::kKeychainDenied);
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
