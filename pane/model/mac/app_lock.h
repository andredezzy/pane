// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_APP_LOCK_H_
#define PANE_MODEL_MAC_APP_LOCK_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "pane/model/data_key.h"
#include "pane/model/mac/data_vault.h"
#include "pane/model/mac/safe_storage.h"
#include "pane/model/pin_entry.h"
#include "pane/model/security.h"

namespace pane::model {

// The app lock (the Electron app's security-store.ts): with a PIN set, Pane
// starts locked behind the PIN screen, and locks while open (ADR 0116); the
// screen also sets, changes, and removes the PIN from Settings. The fifth
// wrong PIN in a row at the lock erases everything (the panic wipe) and starts
// Pane fresh; the fifth in Settings only locks Pane (ADR 0042). The PIN opens
// the data key (ADR 0113), sealed in security.json and in its copy beside the
// volume, which the vault then opens the data with: without a PIN, the key
// opens at launch.
//
// On the window's sequence. The files are read before it, where the disk may
// be (Load), and written on a sequence of their own, which a quit waits for.
class AppLock {
 public:
  // The PIN screen's line when no key can be read (Paper 07.6), when the
  // Keychain gives no secret (07.7), and when the volume would not open (07.8).
  static constexpr char kKeyUnreadable[] =
      "Pane can’t read the key to your data.\n"
      "Quit, then restore security.json.";
  static constexpr char kKeychainDenied[] =
      "Pane can’t read the key to your data.\n"
      "Open Pane again, and allow its keychain item.";
  static constexpr char kDataWontOpen[] =
      "Pane can’t open your encrypted data.\n"
      "Quit, then open Pane again.";

  // security.json, or its copy, as they read.
  struct Loaded {
    SecurityState state;
    // No PIN or key reads from what is there: the lock stays shut.
    bool unreadable = false;
    // No key, with a volume: what it holds cannot be read.
    bool key_lost = false;
  };

  // A security.json that does not decode never opens the lock: its PIN is
  // read from what is left when it can be, its key from the copy beside the
  // volume, and the lock stays shut when neither can. The file is kept aside,
  // as security.json.damaged, before anything writes over it. Blocks on the
  // disk.
  static Loaded Load(const base::FilePath& data_directory);

  class Delegate {
   public:
    virtual ~Delegate() = default;

    // The right PIN opened the lock: what waited behind it goes on.
    virtual void DidUnlock() = 0;

    // The fifth wrong PIN in Settings: Pane locks.
    virtual void DidExhaustSettingsAttempts() = 0;
  };

  class Observer : public base::CheckedObserver {
   public:
    // is_locked(), entry(), or state() changed.
    virtual void OnAppLockChanged() {}

    // The dots shake for 0.4 s; the entry starts over at 0.6 s.
    virtual void OnPinShake() {}
  };

  // What opening the data key came to.
  struct Opened {
    enum class Kind {
      kKey,
      kWrongPin,
      // The Keychain gave no secret: the user denied it, or it failed.
      kNoSecret,
      // The key does not open with the secret: another engine key.
      kOtherSecret,
    };

    Opened();
    Opened(const Opened&);
    Opened& operator=(const Opened&);
    ~Opened();

    Kind kind = Kind::kWrongPin;
    // For kKey: the key, the secret it is sealed under, and the seal, when
    // it was just made.
    Key key = {};
    std::vector<uint8_t> secret;
    std::optional<SealedKey> made;
  };

  // `vault` and `delegate` outlive it.
  AppLock(const base::FilePath& data_directory,
          Loaded loaded,
          DataVault& vault,
          SafeStorage safe_storage,
          Delegate* delegate);
  AppLock(const AppLock&) = delete;
  AppLock& operator=(const AppLock&) = delete;
  ~AppLock();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  const SecurityState& state() const { return state_; }
  // The PIN screen covers the window, and only the PIN opens it.
  bool is_locked() const { return is_locked_; }
  // The PIN screen, while it shows. It takes every key, so nothing reaches a
  // page or the app's shortcuts; only Quit gets through.
  const std::optional<PinEntry>& entry() const { return entry_; }

  // With no PIN, the key opens at launch, off this sequence: the Keychain
  // may ask first. With one, the PIN screen opens it, and the Keychain's
  // secret is found now, find-only, so an unlock doesn't wait on the Keychain
  // after the last digit.
  void Start();

  // Pane locks while open (ADR 0116, Paper 07.9): the PIN screen shows at
  // once, as at launch; the caller closes every profile's data behind it (the
  // vault's Lock). Only a PIN locks.
  void LockNow();

  // The volume would not open once the key had (DataVault): nothing of the
  // profiles can run, so the lock shuts, says so, and takes no keys. With a
  // PIN, the panel that slid away comes back.
  void VolumeWontOpen();

  // Settings' Set up, Change, and Remove PIN.
  void Show(PinEntry::Mode mode);

  // The back arrow, or Escape; never while locked.
  void Dismiss();

  void Digit(char16_t digit);
  void Backspace();
  void ContinueWithPin();

  // What the fifth wrong PIN at the lock does once the key is gone from
  // disk, in place of WipeAndRelaunch, which never returns.
  void SetWipeForTesting(base::RepeatingClosure wipe);

 private:
  void Perform(PinEntry::Action action);
  void Verify(const std::string& pin);
  void PostOpen(std::optional<std::string> pin, bool unlocking);
  void OnOpened(bool unlocking, bool with_pin, Opened opened);
  void OpenVault(const Key& key);
  void Verified(bool matches);
  void Shook();
  void Reseal(std::optional<std::string> pin);
  void Resealed(std::optional<int> length, SealedKey sealed);
  void SecretFound(std::optional<std::vector<uint8_t>> secret);
  void Save();
  void SaveThen(base::OnceClosure then);
  void Wipe();
  void Changed();

  const base::FilePath data_directory_;
  const raw_ref<DataVault> vault_;
  const SafeStorage safe_storage_;
  const raw_ptr<Delegate> delegate_;
  // security.json and its copy, written in order, which a quit waits for.
  const scoped_refptr<base::SequencedTaskRunner> files_;

  SecurityState state_;
  bool is_locked_ = false;
  std::optional<PinEntry> entry_;
  // The secret the open data key is sealed under: setting, changing, or
  // removing the PIN reseals the key with it.
  std::optional<std::vector<uint8_t>> secret_;
  // Wrong PINs in Settings in a row, in memory only: the count on disk is the
  // lock's alone, where its fifth erases everything.
  int settings_attempts_ = 0;
  base::RepeatingClosure wipe_for_testing_;

  base::ObserverList<Observer> observers_;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<AppLock> weak_factory_{this};
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_APP_LOCK_H_
