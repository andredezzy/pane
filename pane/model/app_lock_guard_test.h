// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_APP_LOCK_GUARD_TEST_H_
#define PANE_MODEL_APP_LOCK_GUARD_TEST_H_

#include <stdint.h>

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
#include "base/functional/callback.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/time/time_override.h"
#include "pane/model/app_lock.h"
#include "pane/model/data_key.h"
#include "pane/model/data_lock.h"
#include "pane/model/data_vault.h"
#include "pane/model/folder_place.h"
#include "pane/model/persisted_file.h"
#include "pane/model/pin_entry.h"
#include "pane/model/security.h"
#include "pane/model/stand_in_pin_guard.h"
#include "pane/model/test_waits.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {

// The lock with the PIN's guard (U5), on every system, with a TPM in memory
// (StandInPinGuard) and the engine's secret given in: what the guard adds to
// the seal, and what each of the chip's answers does
// (app_lock_unittest.cc), and on Windows what each of its provider's codes
// does (app_lock_win_unittest.cc). The Mac's own rules, on its volume and
// Keychain, are mac/app_lock_unittest.cc's.

inline constexpr std::string_view kGuardTestSecret = "c2VjcmV0c2VjcmV0c2VjcmV0";

inline base::span<const uint8_t> GuardTestBytes(std::string_view text) {
  return base::as_byte_span(text);
}

class GuardTestVaultDelegate : public DataVault::Delegate {
 public:
  GuardTestVaultDelegate();
  ~GuardTestVaultDelegate() override;

  void DataKeyDidOpen(base::OnceClosure loaded) override;
  std::vector<std::string> ProfileIds() override;
  void AccessDidOpen() override;
  void VolumeWontOpen() override;
  void QuitToCloseVolume() override;
  void VolumeDidDetachAtLock() override;
};

// The app's side: asked to lock, it locks as UiApp's LockNow does, the PIN
// screen at once and then the data closing behind it, the key leaving memory.
class GuardTestLockDelegate : public AppLock::Delegate {
 public:
  GuardTestLockDelegate();
  ~GuardTestLockDelegate() override;

  void DidUnlock() override;
  void DidAskToLock() override;

  int unlocks = 0;
  int asked_to_lock = 0;
  base::RepeatingClosure lock_now;
};

class GuardTestShakes : public AppLock::Observer {
 public:
  GuardTestShakes();
  ~GuardTestShakes() override;

  void OnPinShake() override;

  int count = 0;
};

class AppLockGuardTest : public testing::Test {
 protected:
  AppLockGuardTest();
  ~AppLockGuardTest() override;

  void SetUp() override;
  void TearDown() override;

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
          return base::ToVector(GuardTestBytes(secret_));
        });
    system.guard = guard_;
    system.no_secret = PinEntry::UnreadableReason::kDpapiRefused;
    system.words = words_;
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

  // The lock and the vault gone, as at Pane's end.
  void Quit() {
    task_environment_.RunUntilIdle();
    lock_->RemoveObserver(&shakes_);
    lock_.reset();
    vault_.reset();
  }

  void Save(const SecurityState& state) {
    for (std::string_view name : {"security", "Data.security"}) {
      ASSERT_TRUE(PersistedFile(data_, name).Save(state.ToValue()));
    }
  }

  // The file as saved. A save replaces it, which isn't atomic on Windows
  // (ReplaceFileW moves the old file aside first): a read that meets one
  // finds no file, or one it can't open, and tries again, for up to 5 s of
  // real time, with the test's own sequence left as it is.
  std::optional<SecurityState> Saved(std::string_view name = "security") const {
    const base::TimeTicks end =
        base::subtle::TimeTicksNowIgnoringOverride() + base::Seconds(5);
    do {
      const base::expected<std::optional<base::Value>, FileError> state =
          PersistedFile(data_, name).Load();
      if (state.has_value() && *state) {
        return SecurityState::FromValue(**state);
      }
      base::PlatformThread::Sleep(base::Milliseconds(1));
    } while (base::subtle::TimeTicksNowIgnoringOverride() < end);
    return std::nullopt;
  }

  // A key sealed under kGuardTestSecret with `pin`, the stand-in TPM guarding
  // it, in both files.
  Key SaveGuardedKey(std::string_view pin) {
    const Key key = MakeDataKey();
    const base::expected<Guarded, PinGuard::Refusal> guarded =
        guard_->Guard(pin);
    EXPECT_TRUE(guarded.has_value());
    Save(SecurityState(
        Pin{.length = static_cast<int>(pin.size())}, 0,
        SealDataKey(key, GuardTestBytes(kGuardTestSecret), pin, *guarded)));
    return key;
  }

  // A file the lock can't replace, as one another program holds without
  // sharing its deletion stands on Windows: a folder at its name. Its bytes
  // as they were, for Unhold.
  std::string Hold(std::string_view name) {
    // A save already on its way lands first, or it could take the name back.
    task_environment_.RunUntilIdle();
    const base::FilePath path = data_.AppendASCII(name);
    std::string bytes;
    EXPECT_TRUE(base::ReadFileToString(path, &bytes));
    EXPECT_TRUE(base::DeleteFile(path));
    EXPECT_TRUE(base::CreateDirectory(path));
    EXPECT_TRUE(base::WriteFile(path.AppendASCII("held"), "held"));
    return bytes;
  }

  // Let go of a held file, its bytes back as they were.
  void Unhold(std::string_view name, std::string_view bytes) {
    const base::FilePath path = data_.AppendASCII(name);
    ASSERT_TRUE(base::DeletePathRecursively(path));
    ASSERT_TRUE(base::WriteFile(path, bytes));
  }

  // Let go of a held file, nothing in its place.
  void Release(std::string_view name) {
    ASSERT_TRUE(base::DeletePathRecursively(data_.AppendASCII(name)));
  }

  void Type(std::string_view digits) {
    for (char digit : digits) {
      lock_->Digit(digit);
    }
  }

  // Change PIN in Settings: the current PIN checked, then the new one
  // entered and confirmed.
  void ChangePin(std::string_view current, std::string_view next) {
    lock_->Show(PinEntry::Mode::kChange);
    Type(current);
    ASSERT_TRUE(WaitFor([&] {
      return lock_->entry() && lock_->entry()->step() == PinEntry::Step::kEnter;
    }));
    Type(next);
    lock_->ContinueWithPin();
    Type(next);
  }

  [[nodiscard]] bool Ready() {
    return WaitFor([&] { return lock_->entry() && !lock_->entry()->busy(); });
  }

  // The disk refused the reseal, and the screen says so (Paper 07.12).
  [[nodiscard]] bool Unsaved(int writes = 1) {
    return WaitFor([&] {
      return lock_->entry() && lock_->entry()->is_unsaved() &&
             lock_->entry()->unsaved_writes() == writes;
    });
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
  std::string secret_{kGuardTestSecret};
  // DPAPI gives no secret.
  bool no_secret_ = false;
  // The launch's words for each state, as Windows' gives its own
  // (data_launch_win.cc): each its own here, so a test reads which shows.
  AppLock::System::Words words_ = [] {
    AppLock::System::Words words;
    words.key_unreadable = "key unreadable";
    words.no_secret = "no secret";
    words.data_wont_open = "data won't open";
    words.guard_gone = "guard gone";
    words.guard_unavailable = "guard unavailable";
    words.locked_out = "locked out";
    words.pin_not_saved = "pin not saved";
    words.guard_refused = "guard refused";
    return words;
  }();
  scoped_refptr<StandInPinGuard> guard_;
  GuardTestVaultDelegate vault_delegate_;
  GuardTestLockDelegate lock_delegate_;
  GuardTestShakes shakes_;
  int wipes_ = 0;
  std::unique_ptr<DataVault> vault_;
  std::unique_ptr<AppLock> lock_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_APP_LOCK_GUARD_TEST_H_
