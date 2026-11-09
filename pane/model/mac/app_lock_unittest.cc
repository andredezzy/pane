// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/app_lock.h"

#include <Security/Security.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "pane/model/data_key.h"
#include "pane/model/mac/data_lock.h"
#include "pane/model/mac/data_vault.h"
#include "pane/model/mac/data_volume.h"
#include "pane/model/mac/panic_wipe_run.h"
#include "pane/model/mac/test_keychain.h"
#include "pane/model/mac/vault_test_support.h"
#include "pane/model/persisted_file.h"
#include "pane/model/pin_entry.h"
#include "pane/model/security.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// The app lock's rules (ADR 0042, 0113, 0116): the Swift app's AppLock had no
// unit tests, its scenarios ran them (security, data-volume). These run it on
// the Keychain and the disk image tools in memory, and on security.json and
// its copy as they are on disk.

// The Keychain item's stored bytes: Chromium's 16 random bytes, as base64
// text.
constexpr std::string_view kSecret = "c2VjcmV0c2VjcmV0c2VjcmV0";
constexpr std::string_view kOtherSecret = "b3RoZXJvdGhlcm90aGVyb3Ro";

// An earlier Pane's PIN, 1234, as the Electron app's bcryptjs hashed it.
constexpr char kBcryptHash[] =
    "$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO";

base::span<const uint8_t> Bytes(std::string_view text) {
  return base::as_byte_span(text);
}

class VaultDelegate : public DataVault::Delegate {
 public:
  void DataKeyDidOpen() override {}
  std::vector<std::string> ProfileIds() override { return {}; }
  void AccessDidOpen() override {}
  // As the app does: the lock says so.
  void VolumeWontOpen() override { lock->VolumeWontOpen(); }
  void QuitToCloseVolume() override {}
  void VolumeDidDetachAtLock() override {}

  raw_ptr<AppLock> lock = nullptr;
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

class AppLockTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    data_ = directory_.GetPath().Append("data");
    ASSERT_TRUE(base::CreateDirectory(data_));
    tools_ =
        base::MakeRefCounted<FakeVolumeTools>(DataVolume(data_, std::nullopt));
    migrator_ = base::MakeRefCounted<FakeFileMigrator>();
  }

  void TearDown() override {
    migrator_->ReleaseAll();
    // The Keychain's work done before the Keychain goes.
    task_environment_.RunUntilIdle();
    if (lock_) {
      lock_->RemoveObserver(&shakes_);
    }
    vault_delegate_.lock = nullptr;
    lock_.reset();
    vault_.reset();
  }

  // The vault and the lock, as the app makes them at launch, and the lock
  // started.
  AppLock& Launch() {
    vault_ = std::make_unique<DataVault>(data_, std::nullopt, tools_,
                                         DataLock::Take(data_), nullptr,
                                         &vault_delegate_);
    vault_->set_file_migrator(migrator_->Get());
    lock_ =
        std::make_unique<AppLock>(data_, AppLock::Load(data_), *vault_,
                                  keychain_.safe_storage(), &lock_delegate_);
    vault_delegate_.lock = lock_.get();
    lock_->SetWipeForTesting(base::BindLambdaForTesting([&] { ++wipes_; }));
    lock_->AddObserver(&shakes_);
    lock_->Start();
    return *lock_;
  }

  void Save(const SecurityState& state, std::string_view name = "security") {
    ASSERT_TRUE(PersistedFile(data_, name).Save(state.ToValue()));
  }

  // security.json, or its copy, as it is on disk now.
  std::optional<SecurityState> Saved(std::string_view name = "security") const {
    const base::expected<std::optional<base::Value>, FileError> state =
        PersistedFile(data_, name).Load();
    if (!state.has_value() || !*state) {
      return std::nullopt;
    }
    return SecurityState::FromValue(**state);
  }

  // A key sealed under kSecret, with `pin`, saved in both files.
  Key SaveKey(std::optional<std::string_view> pin) {
    const Key key = MakeDataKey();
    const SecurityState state(
        pin ? std::make_optional(Pin{.length = static_cast<int>(pin->size())})
            : std::nullopt,
        0, SealDataKey(key, Bytes(kSecret), pin));
    Save(state);
    Save(state, "Data.security");
    return key;
  }

  void Type(std::string_view digits) {
    for (char digit : digits) {
      lock_->Digit(digit);
    }
  }

  // The PIN screen ready for digits again, once a check has answered.
  [[nodiscard]] bool Ready() {
    return WaitFor([&] { return lock_->entry() && !lock_->entry()->busy(); });
  }

  // The dots shook: the entry starts over at 0.6 s.
  void Shook(int count) {
    ASSERT_TRUE(WaitFor([&] { return shakes_.count == count; }));
    task_environment_.AdvanceClock(base::Milliseconds(600));
    base::RunLoop().RunUntilIdle();
  }

  // Waits until security.json and its copy both hold a key, with the PIN of
  // `length` digits, or none: the files are written behind the state.
  [[nodiscard]] bool SavedWithPin(std::optional<int> length) const {
    return WaitFor([&] {
      for (std::string_view name : {"security", "Data.security"}) {
        const std::optional<SecurityState> state = Saved(name);
        if (!state || !state->key ||
            (state->pin ? std::make_optional(state->pin->length)
                        : std::nullopt) != length) {
          return false;
        }
      }
      return true;
    });
  }

  bool OpensWith(const SecurityState& state,
                 const Key& key,
                 std::optional<std::string_view> pin) const {
    if (!state.key) {
      return false;
    }
    const base::expected<Key, OpenError> opened =
        OpenDataKey(*state.key, Bytes(kSecret), pin);
    return opened.has_value() && *opened == key;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir directory_;
  base::FilePath data_;
  TestKeychain keychain_;
  scoped_refptr<FakeVolumeTools> tools_;
  scoped_refptr<FakeFileMigrator> migrator_;
  VaultDelegate vault_delegate_;
  LockDelegate lock_delegate_;
  Shakes shakes_;
  int wipes_ = 0;
  std::unique_ptr<DataVault> vault_;
  std::unique_ptr<AppLock> lock_;
};

// The right PIN opens the key, and the vault with it. The Keychain's secret
// was found at launch, find-only, so the unlock asks it nothing more.
TEST_F(AppLockTest, UnlocksWithTheRightPin) {
  keychain_.SetEngineSecret(kSecret);
  const Key key = SaveKey("1234");
  AppLock& lock = Launch();
  EXPECT_TRUE(lock.is_locked());
  ASSERT_TRUE(lock.entry());
  EXPECT_EQ(lock.entry()->mode(), PinEntry::Mode::kUnlock);
  EXPECT_EQ(lock.entry()->Dots(), 4);
  ASSERT_TRUE(WaitFor([&] { return keychain_.reads() == 1; }));

  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
  EXPECT_EQ(lock_delegate_.unlocks, 1);
  EXPECT_FALSE(lock.entry());
  EXPECT_EQ(vault_->data_key(), key);
  ASSERT_TRUE(WaitFor([&] { return vault_->is_attached(); }));
  EXPECT_EQ(keychain_.reads(), 1);
  EXPECT_EQ(Saved()->failed_attempts, 0);
}

// A wrong PIN at the lock counts at once, on disk, in both files, so a quit
// can't reset it; the fifth takes the key in the same save, and the wipe is
// due (ADR 0042, 0113).
TEST_F(AppLockTest, AWrongPinCountsTowardTheWipe) {
  keychain_.SetEngineSecret(kSecret);
  SaveKey("1234");
  AppLock& lock = Launch();

  for (int attempt = 1; attempt < SecurityState::kMaxAttempts; ++attempt) {
    ASSERT_TRUE(Ready());
    Type("1111");
    Shook(attempt);
    EXPECT_EQ(Saved()->failed_attempts, attempt);
    EXPECT_EQ(Saved("Data.security")->failed_attempts, attempt);
    EXPECT_TRUE(Saved()->key);
    EXPECT_FALSE(IsWipeDue(data_));
  }

  ASSERT_TRUE(Ready());
  Type("1111");
  ASSERT_TRUE(WaitFor([&] { return wipes_ == 1; }));
  EXPECT_EQ(Saved()->failed_attempts, SecurityState::kMaxAttempts);
  EXPECT_FALSE(Saved()->key);
  EXPECT_FALSE(Saved("Data.security")->key);
  EXPECT_TRUE(IsWipeDue(data_));
  EXPECT_TRUE(lock.is_locked());
  EXPECT_TRUE(tools_->calls().empty());
}

// The right PIN after wrong ones starts the count over.
TEST_F(AppLockTest, TheRightPinStartsTheCountOver) {
  keychain_.SetEngineSecret(kSecret);
  SaveKey("1234");
  AppLock& lock = Launch();
  ASSERT_TRUE(Ready());
  Type("1111");
  Shook(1);
  EXPECT_EQ(Saved()->failed_attempts, 1);

  ASSERT_TRUE(Ready());
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
  EXPECT_TRUE(WaitFor([&] {
    return Saved()->failed_attempts == 0 &&
           Saved("Data.security")->failed_attempts == 0;
  }));
}

// A key sealed under another secret (the engine's item replaced) is no wrong
// PIN: the lock says it can't read the key, and counts nothing.
TEST_F(AppLockTest, AnotherSecretCountsNothing) {
  keychain_.SetEngineSecret(kSecret);
  SaveKey("1234");
  keychain_.SetEngineSecret(kOtherSecret);
  AppLock& lock = Launch();
  ASSERT_TRUE(Ready());

  Type("1234");
  ASSERT_TRUE(
      WaitFor([&] { return lock.entry() && lock.entry()->is_unreadable(); }));
  EXPECT_EQ(lock.entry()->error(), AppLock::kKeyUnreadable);
  EXPECT_TRUE(lock.is_locked());
  EXPECT_EQ(Saved()->failed_attempts, 0);
  EXPECT_TRUE(Saved()->key);

  // And it takes no more digits.
  Type("5678");
  RunFor(base::Milliseconds(50));
  EXPECT_EQ(Saved()->failed_attempts, 0);
  EXPECT_EQ(shakes_.count, 0);
}

// No secret from the Keychain (denied, or a locked session): the lock asks
// to open Pane again and allow its item (Paper 07.7), and counts nothing.
TEST_F(AppLockTest, NoSecretAsksToAllowTheItem) {
  keychain_.SetEngineSecret(kSecret);
  SaveKey("1234");
  keychain_.FailWith(errSecAuthFailed);
  AppLock& lock = Launch();
  ASSERT_TRUE(Ready());

  Type("1234");
  ASSERT_TRUE(
      WaitFor([&] { return lock.entry() && lock.entry()->is_unreadable(); }));
  EXPECT_EQ(lock.entry()->error(), AppLock::kKeychainDenied);
  EXPECT_EQ(Saved()->failed_attempts, 0);
}

// security.json lost: its copy beside the volume stands in for it.
TEST_F(AppLockTest, TheCopyStandsInForALostKey) {
  keychain_.SetEngineSecret(kSecret);
  const Key key = SaveKey("1234");
  ASSERT_TRUE(base::DeleteFile(data_.Append("security.json")));
  AppLock& lock = Launch();
  EXPECT_TRUE(lock.is_locked());
  ASSERT_TRUE(Ready());

  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
  EXPECT_EQ(vault_->data_key(), key);
  ASSERT_TRUE(WaitFor([&] { return Saved().has_value(); }));
  EXPECT_TRUE(OpensWith(*Saved(), key, "1234"));
}

// A security.json that no longer decodes, its key gone with it: the copy's
// key stands in, and the damaged file is kept aside before anything writes
// over it.
TEST_F(AppLockTest, ADamagedFileTakesTheCopysKey) {
  keychain_.SetEngineSecret(kSecret);
  const Key key = SaveKey("1234");
  ASSERT_TRUE(base::WriteFile(data_.Append("security.json"),
                              R"({"state":{"failedAttempts":2,"pin":)"));
  AppLock& lock = Launch();
  EXPECT_TRUE(base::PathExists(data_.Append("security.json.damaged")));
  EXPECT_EQ(lock.state().failed_attempts, 0);
  ASSERT_TRUE(Ready());

  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
  EXPECT_EQ(vault_->data_key(), key);
}

// An earlier Pane's damaged security.json, its PIN still a bcrypt hash and
// no key sealed yet: the PIN and the count are read from what is left, and
// the PIN is checked against the hash once, then seals a new key, saved
// before the volume it opens is made (ADR 0113).
TEST_F(AppLockTest, RecoversAnEarlierPanesPinAndSealsTheKeyWithIt) {
  keychain_.SetEngineSecret(kSecret);
  ASSERT_TRUE(base::WriteFile(
      data_.Append("security.json"),
      base::StrCat({R"({"state":{"failedAttempts":2,"pin":{"hash":")",
                    kBcryptHash, R"(","length":4})"})));
  bool saved_before_the_attach = false;
  tools_->SetOnAttach(base::BindLambdaForTesting(
      [&] { saved_before_the_attach = Saved().has_value() && Saved()->key; }));
  AppLock& lock = Launch();
  EXPECT_TRUE(lock.is_locked());
  EXPECT_EQ(lock.state().failed_attempts, 2);
  ASSERT_TRUE(lock.state().pin);
  EXPECT_EQ(lock.state().pin->length, 4);
  ASSERT_TRUE(Ready());

  // A wrong one counts, against the hash too.
  Type("1111");
  Shook(1);
  EXPECT_EQ(Saved()->failed_attempts, 3);
  ASSERT_TRUE(Ready());

  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
  ASSERT_TRUE(WaitFor([&] { return vault_->is_attached(); }));
  EXPECT_TRUE(saved_before_the_attach);
  const std::optional<SecurityState> saved = Saved();
  ASSERT_TRUE(saved);
  EXPECT_EQ(saved->failed_attempts, 0);
  ASSERT_TRUE(saved->pin);
  EXPECT_FALSE(saved->pin->hash);
  EXPECT_TRUE(OpensWith(*saved, *vault_->data_key(), "1234"));
  EXPECT_TRUE(OpensWith(*Saved("Data.security"), *vault_->data_key(), "1234"));
}

// Nothing to read a PIN or a key from: the lock stays shut, and says why.
TEST_F(AppLockTest, NothingReadableStaysShut) {
  keychain_.SetEngineSecret(kSecret);
  ASSERT_TRUE(base::WriteFile(data_.Append("security.json"), "not json"));
  AppLock& lock = Launch();
  EXPECT_TRUE(lock.is_locked());
  ASSERT_TRUE(lock.entry());
  EXPECT_TRUE(lock.entry()->is_unreadable());
  EXPECT_EQ(lock.entry()->error(), AppLock::kKeyUnreadable);
  RunFor(base::Milliseconds(20));
  EXPECT_TRUE(tools_->calls().empty());
}

// A volume no key opens: what it holds can't be read, so the lock stays
// shut, and nothing makes a new key over it.
TEST_F(AppLockTest, AVolumeWithNoKeyStaysShut) {
  keychain_.SetEngineSecret(kSecret);
  ASSERT_TRUE(base::CreateDirectory(DataVolume(data_, std::nullopt).image()));
  AppLock& lock = Launch();
  EXPECT_TRUE(lock.is_locked());
  EXPECT_EQ(lock.entry()->error(), AppLock::kKeyUnreadable);
  RunFor(base::Milliseconds(20));
  EXPECT_TRUE(tools_->calls().empty());
  EXPECT_FALSE(Saved());
}

// A fresh install: no PIN, no key, no item yet. The key opens at launch, the
// secret made as Chromium makes it, and the key is saved, in both files,
// before the volume it opens is made.
TEST_F(AppLockTest, MakesTheKeyAtTheFirstLaunch) {
  bool saved_before_the_attach = false;
  tools_->SetOnAttach(base::BindLambdaForTesting(
      [&] { saved_before_the_attach = Saved().has_value() && Saved()->key; }));
  AppLock& lock = Launch();
  EXPECT_FALSE(lock.is_locked());
  ASSERT_TRUE(WaitFor([&] { return vault_->is_attached(); }));
  EXPECT_TRUE(saved_before_the_attach);

  const std::optional<std::string> secret = keychain_.EngineSecret();
  ASSERT_TRUE(secret);
  EXPECT_EQ(secret->size(), 24u);
  const std::optional<SecurityState> saved = Saved();
  ASSERT_TRUE(saved && saved->key);
  EXPECT_FALSE(saved->pin);
  const base::expected<Key, OpenError> opened =
      OpenDataKey(*saved->key, Bytes(*secret), std::nullopt);
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(*opened, vault_->data_key());
  EXPECT_EQ(Saved("Data.security"), saved);
}

// 0.18.0 counted wrong PINs from Settings too: a fifth attempt with the key
// still there leaves the lock its attempts.
TEST_F(AppLockTest, AFifthAttemptWithAKeyStartsOver) {
  keychain_.SetEngineSecret(kSecret);
  const Key key = MakeDataKey();
  Save(SecurityState(Pin{.length = 4}, SecurityState::kMaxAttempts,
                     SealDataKey(key, Bytes(kSecret), "1234")));
  AppLock& lock = Launch();
  EXPECT_EQ(lock.state().failed_attempts, 0);
  ASSERT_TRUE(Ready());
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));
}

// Set up, change, and remove the PIN: each seals the same key again, with a
// new salt, and asks the Keychain nothing more.
TEST_F(AppLockTest, SettingThePinResealsTheKey) {
  keychain_.SetEngineSecret(kSecret);
  const Key key = SaveKey(std::nullopt);
  AppLock& lock = Launch();
  ASSERT_TRUE(WaitFor([&] { return vault_->is_attached(); }));
  const int reads = keychain_.reads();

  lock.Show(PinEntry::Mode::kSetup);
  Type("1234");
  lock.ContinueWithPin();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.entry() && lock.state().pin; }));
  ASSERT_TRUE(SavedWithPin(4));
  std::optional<SecurityState> saved = Saved();
  ASSERT_TRUE(saved && saved->pin && saved->key && saved->key->pin);
  EXPECT_EQ(saved->pin->length, 4);
  EXPECT_TRUE(OpensWith(*saved, key, "1234"));
  EXPECT_FALSE(OpensWith(*saved, key, std::nullopt));
  const std::string salt = saved->key->pin->salt;

  lock.Show(PinEntry::Mode::kChange);
  Type("1234");
  ASSERT_TRUE(WaitFor([&] {
    return lock.entry() && lock.entry()->step() == PinEntry::Step::kEnter;
  }));
  Type("567890");
  lock.ContinueWithPin();
  Type("567890");
  ASSERT_TRUE(WaitFor([&] {
    return !lock.entry() && lock.state().pin && lock.state().pin->length == 6;
  }));
  ASSERT_TRUE(SavedWithPin(6));
  saved = Saved();
  ASSERT_TRUE(saved && saved->key && saved->key->pin);
  EXPECT_TRUE(OpensWith(*saved, key, "567890"));
  EXPECT_FALSE(OpensWith(*saved, key, "1234"));
  EXPECT_NE(saved->key->pin->salt, salt);
  EXPECT_EQ(Saved("Data.security"), saved);

  lock.Show(PinEntry::Mode::kRemove);
  Type("567890");
  ASSERT_TRUE(WaitFor([&] { return !lock.entry() && !lock.state().pin; }));
  ASSERT_TRUE(SavedWithPin(std::nullopt));
  saved = Saved();
  ASSERT_TRUE(saved && saved->key);
  EXPECT_FALSE(saved->pin);
  EXPECT_TRUE(OpensWith(*saved, key, std::nullopt));
  EXPECT_EQ(keychain_.reads(), reads);
}

// The fifth wrong PIN in Settings only locks Pane: those attempts never reach
// the disk, where the lock's fifth erases everything.
TEST_F(AppLockTest, TheFifthWrongPinInSettingsLocks) {
  keychain_.SetEngineSecret(kSecret);
  SaveKey("1234");
  AppLock& lock = Launch();
  ASSERT_TRUE(Ready());
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));

  lock.Show(PinEntry::Mode::kChange);
  for (int attempt = 1; attempt <= SecurityState::kMaxAttempts; ++attempt) {
    ASSERT_TRUE(Ready());
    Type("1111");
    Shook(attempt);
  }
  EXPECT_EQ(lock_delegate_.exhausted, 1);
  EXPECT_EQ(Saved()->failed_attempts, 0);
  EXPECT_TRUE(Saved()->key);
  EXPECT_EQ(wipes_, 0);
}

// A Settings sheet dismissed while its wrong PIN is checked: the attempt
// still counts toward the five, and nothing shuts the lock. The Swift took
// the sheet gone for a key no PIN of security.json opens, and shut it until
// Pane was opened again.
TEST_F(AppLockTest, ADismissedSheetsWrongPinCountsAndLocksNothing) {
  keychain_.SetEngineSecret(kSecret);
  SaveKey("1234");
  AppLock& lock = Launch();
  ASSERT_TRUE(Ready());
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.is_locked(); }));

  lock.Show(PinEntry::Mode::kChange);
  Type("1111");
  lock.Dismiss();
  RunFor(base::Milliseconds(300));
  EXPECT_FALSE(lock.is_locked());
  EXPECT_FALSE(lock.entry());

  lock.Show(PinEntry::Mode::kChange);
  for (int attempt = 1; attempt < SecurityState::kMaxAttempts; ++attempt) {
    ASSERT_TRUE(Ready());
    Type("1111");
    Shook(attempt);
  }
  EXPECT_EQ(lock_delegate_.exhausted, 1);
}

// A key sealed with a PIN that security.json no longer names: no PIN opens
// it at launch, so the lock says it can't read the key.
TEST_F(AppLockTest, AKeySealedWithAPinNoFileNames) {
  keychain_.SetEngineSecret(kSecret);
  Save(SecurityState(std::nullopt, 0,
                     SealDataKey(MakeDataKey(), Bytes(kSecret), "1234")));
  AppLock& lock = Launch();
  ASSERT_TRUE(WaitFor([&] { return lock.is_locked(); }));
  ASSERT_TRUE(lock.entry());
  EXPECT_EQ(lock.entry()->error(), AppLock::kKeyUnreadable);
  EXPECT_EQ(Saved()->failed_attempts, 0);
  EXPECT_TRUE(tools_->calls().empty());
}

// Pane locks while open only with a PIN (ADR 0116), and the PIN screen shows
// at once.
TEST_F(AppLockTest, LocksOnlyWithAPin) {
  keychain_.SetEngineSecret(kSecret);
  SaveKey(std::nullopt);
  AppLock& lock = Launch();
  // Settings shows once the key has opened the data.
  ASSERT_TRUE(WaitFor([&] { return vault_->data_key().has_value(); }));
  lock.LockNow();
  EXPECT_FALSE(lock.is_locked());

  lock.Show(PinEntry::Mode::kSetup);
  Type("1234");
  lock.ContinueWithPin();
  Type("1234");
  ASSERT_TRUE(WaitFor([&] { return !lock.entry() && lock.state().pin; }));
  lock.LockNow();
  EXPECT_TRUE(lock.is_locked());
  ASSERT_TRUE(lock.entry());
  EXPECT_EQ(lock.entry()->mode(), PinEntry::Mode::kUnlock);
  EXPECT_EQ(lock.entry()->Dots(), 4);
  EXPECT_FALSE(lock.entry()->dismissable());
}

// The volume would not open once the key had (Paper 07.8): the lock shuts,
// says so, and takes no keys.
TEST_F(AppLockTest, AVolumeThatWontOpenShutsTheLock) {
  keychain_.SetEngineSecret(kSecret);
  SaveKey("1234");
  tools_->FailOpens(VolumeError::NotEncrypted("disk90s1"));
  AppLock& lock = Launch();
  ASSERT_TRUE(Ready());

  Type("1234");
  ASSERT_TRUE(
      WaitFor([&] { return lock.entry() && lock.entry()->is_unreadable(); }));
  EXPECT_TRUE(lock.is_locked());
  EXPECT_EQ(lock.entry()->error(), AppLock::kDataWontOpen);
}

}  // namespace
}  // namespace pane::model
