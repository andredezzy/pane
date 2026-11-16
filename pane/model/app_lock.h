// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_APP_LOCK_H_
#define PANE_MODEL_APP_LOCK_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "pane/model/data_key.h"
#include "pane/model/data_vault.h"
#include "pane/model/pin_entry.h"
#include "pane/model/pin_guard.h"
#include "pane/model/security.h"

namespace pane::model {

// The app lock (the Electron app's security-store.ts): with a PIN set, Pane
// starts locked behind the PIN screen, and locks while open (ADR 0116); the
// screen also sets, changes, and removes the PIN from Settings. The fifth
// wrong PIN in a row at the lock erases everything (the panic wipe) and starts
// Pane fresh, unless a chip guards the seal, where the chip's and Windows' own
// lockouts bound the guesses and nothing is wiped (ADR 0113); the fifth in
// Settings only locks Pane (ADR 0042). The PIN opens
// the data key (ADR 0113), sealed in security.json and in its copy beside the
// volume, which the vault then opens the data with: without a PIN, the key
// opens at launch. On Windows a key the TPM holds guards the PIN in the seal
// (PinGuard, U5): a wrong PIN there is the chip's refusal, counted by the chip
// as well, and the panic wipe deletes that key with the data key.
//
// On the window's sequence. The files are read before it, where the disk may
// be (Load), and written on a sequence of their own, which a quit waits for.
class AppLock {
 public:
  // What the lock asks of the system it runs on (ADR 0124).
  struct System {
    System();
    System(const System&);
    System& operator=(const System&);
    ~System();

    // The secret the data key is sealed under, found, or made where none is
    // and `makes_one`: the Keychain's on the Mac (SafeStorage), os_crypt's
    // under DPAPI on Windows (engine_secret.h). nullopt when the system gave
    // none. Blocks, and may wait on the user: any sequence that may.
    base::RepeatingCallback<std::optional<std::vector<uint8_t>>(bool makes_one)>
        secret;
    // Whether a PIN matches the bcrypt hash an earlier Pane kept, checked
    // once (the Mac's, ADR 0042); null where no Pane kept one.
    base::RepeatingCallback<bool(std::string_view pin, std::string_view hash)>
        matches_hash;
    // What guards the PIN in the seal: the TPM's on Windows; null on the Mac.
    scoped_refptr<PinGuard> guard;
    // Why the lock says no key reads when `secret` gives none: the
    // Keychain's refusal on the Mac, DPAPI's on Windows.
    PinEntry::UnreadableReason no_secret =
        PinEntry::UnreadableReason::kKeychainDenied;
    // The panic wipe, once the key is gone from disk: this instance ends,
    // and the one it starts erases (PanicWipeAtLaunch). Never returns.
    base::RepeatingClosure wipe;

    // The PIN screen's lines, in the system's own words: the Mac's by
    // default (Paper 07.6 to 07.8), Windows' from its launch (11.7's Exit
    // and PC, and the chip's states, 07.1.1, 07.3.1, 07.6.1, 07.6.2, and
    // 07.7.1). Two lines each, split at the newline.
    struct Words {
      Words();
      Words(const Words&);
      Words& operator=(const Words&);
      ~Words();

      // No key the secret opens (07.6).
      std::string key_unreadable = kKeyUnreadable;
      // The system's store gave no secret (07.7, 07.7.1).
      std::string no_secret = kKeychainDenied;
      // The data's place would not open (07.8).
      std::string data_wont_open = kDataWontOpen;
      // The PIN's guard lost its key (07.6.1), or has no chip now (07.6.2).
      std::string guard_gone = kKeyUnreadable;
      std::string guard_unavailable = kKeyUnreadable;
      // The guard's lockout refuses every PIN (07.1.1), a new PIN the guard
      // wouldn't take (07.3.1), and the chip refusing the current PIN in
      // Settings' Change and Remove PIN (07.4.2): shown with the shake, until
      // the next digit; none, the shake alone.
      std::string locked_out;
      std::string pin_not_saved;
      std::string guard_refused;
    };
    Words words;
  };

  // The PIN screen's line when no key can be read (Paper 07.6), when the
  // Keychain gives no secret (07.7), and when the volume would not open (07.8),
  // in the Mac's words, the System's own unless a launch gives others.
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
    // No key, with data a key opened: what it holds cannot be read.
    bool key_lost = false;
  };

  // A security.json that does not decode never opens the lock: its PIN is
  // read from what is left when it can be, its key from the copy beside the
  // volume, and the lock stays shut when neither can. The file is kept aside,
  // as security.json.damaged, before anything writes over it. `holds_data`:
  // whether data a key opened is there, which no key then reads (the Mac's
  // volume, Windows' sealed profiles.json). Blocks on the disk.
  static Loaded Load(const base::FilePath& data_directory, bool holds_data);

  class Delegate {
   public:
    virtual ~Delegate() = default;

    // The right PIN opened the lock: what waited behind it goes on.
    virtual void DidUnlock() = 0;

    // Pane is to lock, as Lock Pane locks it: every profile closes behind
    // the PIN screen, and the data key leaves memory (LockNow, which the app
    // calls). At the fifth wrong PIN in Settings; at the chip's key gone while
    // Settings checked the PIN, where the screen then shows its loss (Paper
    // 07.13 B, 07.4.1); and for a lock asked for while a new seal was on its
    // way to the disk, once it has landed or not.
    virtual void DidAskToLock() = 0;
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
      // The PIN's guard took no PIN, the right one included: the chip's
      // lockout. Nothing counts.
      kLockedOut,
      // The PIN's guard lost its key: deleted, or the chip cleared. What it
      // guarded is lost.
      kGuardGone,
      // The PIN's guard has no chip now for a seal that took one, or its
      // provider refuses: what it guards opens once the chip is back.
      kGuardUnavailable,
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

  // The Keychain's secret as the launch looked for it, before the engine's
  // OSCrypt first reads it (docs/plans/views-migration.md, C5): made there the
  // first time, and only there, since Chromium's GetPassword updates an item
  // that appears between its find and its add, so a second maker could leave
  // the Keychain one secret and the key sealed under the other. The key opens
  // with it, the Keychain asked nothing more; without one, the lock only looks
  // for it, and says so when the Keychain gives none (Paper 07.7).
  struct LaunchSecret {
    std::optional<std::vector<uint8_t>> secret;
  };

  // `vault` and `delegate` outlive it. Without `launch_secret` (no launch
  // looked: the tests), the lock finds the secret, or makes it, itself, as the
  // Swift app's did.
  AppLock(const base::FilePath& data_directory,
          Loaded loaded,
          DataVault& vault,
          System system,
          Delegate* delegate,
          std::optional<LaunchSecret> launch_secret = std::nullopt);
  AppLock(const AppLock&) = delete;
  AppLock& operator=(const AppLock&) = delete;
  ~AppLock();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  const SecurityState& state() const { return state_; }
  // The wrong PINs at the lock in a row that count toward the panic wipe,
  // which the PIN screen shows with the attempts left (ADR 0042); none on a
  // seal the TPM guards, which never wipes, where Windows' own count, which
  // ages and takes Settings' tries too, isn't Pane's to read (ADR 0113,
  // Paper 07.1.1).
  std::optional<int> AttemptsTowardWipe() const;
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
  // vault's Lock). Only a PIN locks. Where the chip lost its key while
  // Settings checked the PIN, the screen shows that loss, for good (07.13 B,
  // 07.4.1). While a new seal is on its way to the disk, the lock waits for
  // it, and is asked for again once it lands (DidAskToLock); while the disk's
  // refusal of one shows (07.12), the lock keeps the seal the disk holds, as
  // Esc does there, and then locks if that seal has a PIN.
  void LockNow();

  // The volume would not open once the key had (DataVault): nothing of the
  // profiles can run, so the lock shuts, says so, and takes no keys. With a
  // PIN, the panel that slid away comes back.
  void VolumeWontOpen();

  // Settings' Set up, Change, and Remove PIN.
  void Show(PinEntry::Mode mode);

  // The back arrow, or Escape; never while locked. Where the disk refused a
  // new seal (Paper 07.12), it keeps the seal the disk holds (KeepSaved).
  void Dismiss();

  void Digit(char16_t digit);
  void Backspace();
  // Return, or Continue. Where the disk refused a new seal (Paper 07.12), it
  // writes it again (TryAgain).
  void ContinueWithPin();

  // The disk refused a new seal (Paper 07.12): Try again writes the same seal
  // again, both files, the chip not asked; once security.json takes it, the
  // screen sinks, and if it fails again its lines stay, nothing shaking.
  void TryAgain();
  // Keep earlier PIN, Keep PIN, or Cancel: the seal the disk holds stays in
  // force, written again; a key the chip made for the new PIN goes only once
  // both files hold it (W8's rule the other way round), and the screen sinks.
  void KeepSaved();

  // What the fifth wrong PIN at the lock does once the key is gone from
  // disk, in place of the system's wipe, which never returns.
  void SetWipeForTesting(base::RepeatingClosure wipe);

 private:
  // Which of security.json and its copy a save wrote.
  struct WrittenFiles {
    bool security = false;
    bool copy = false;

    bool both() const { return security && copy; }
  };

  // security.json, then its copy beside the volume. Blocks on the disk.
  static WrittenFiles WriteSecurityFiles(const base::FilePath& data_directory,
                                         base::Value state);

  void Perform(PinEntry::Action action);
  void Verify(const std::string& pin);
  void PostOpen(std::optional<std::string> pin, bool unlocking);
  void OnOpened(bool unlocking, bool with_pin, Opened opened);
  void OpenVault(const Key& key);
  void Verified(bool matches);
  // The guard refused whatever was typed: the dots shake, nothing counts,
  // and `why` shows until the next digit.
  void Refused(const std::string& why);
  void Shook();
  void Reseal(std::optional<std::string> pin);
  void Resealed(std::optional<int> length, std::optional<SealedKey> sealed);
  // Writes the reseal's new seal, both files; ResealSaved answers.
  void SaveResealed();
  // The new seal's write landed or not: in force once security.json holds
  // it, which the next launch reads first, the screen sinking; else the
  // screen says so (Paper 07.12).
  void ResealSaved(std::optional<std::string> saved_guard,
                   WrittenFiles written);
  // A lock asked for while a reseal waited is asked for again, once it ended.
  void LockIfAsked();
  void ForgetGuardThen(std::optional<std::string> kept, base::OnceClosure then);
  void SecretFound(std::optional<std::vector<uint8_t>> secret);
  void Save();
  void SaveThen(base::OnceClosure then);
  // A save landed: the guard's keys a reseal left go then, once both files
  // were written with the seal that replaced them.
  void Saved(base::OnceClosure then,
             std::optional<std::string> saved_guard,
             WrittenFiles written);
  void Wipe();
  void Changed();
  // Whether wrong PINs at the lock lead to the panic wipe: on every seal but
  // one the TPM guards, whatever the system (ADR 0113).
  bool WrongPinsWipe() const;

  const base::FilePath data_directory_;
  const raw_ref<DataVault> vault_;
  const System system_;
  const raw_ptr<Delegate> delegate_;
  // Whether the lock may make the Keychain's secret: never once a launch
  // looked for it (LaunchSecret).
  const bool makes_secret_;
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
  // The launch's find-only read of the Keychain's secret, while it is under
  // way, and a PIN typed meanwhile, which waits for it rather than asking the
  // Keychain again: where the Keychain asks the user first, a second ask is a
  // second prompt.
  struct WaitingOpen {
    std::string pin;
    bool unlocking = false;
  };
  bool finding_secret_ = false;
  std::optional<WaitingOpen> waiting_open_;
  base::RepeatingClosure wipe_for_testing_;
  // The guard's keys a reseal leaves, which go once a save of the seal that
  // replaced them lands, both files written: until then the seal on the disk
  // may still name one of them, and the next launch would need it. `kept`,
  // the new seal's key; none once the PIN is removed.
  struct ForgetOnceSaved {
    std::optional<std::string> kept;
  };
  std::optional<ForgetOnceSaved> forget_once_saved_;
  // Settings' reseal, from the digit that asked for it until its seal is in
  // force or the seal the disk holds is kept (Paper 07.12). Until then
  // `state_` is the seal the disk holds, which a lock opens with, as the
  // next launch does.
  struct Resealing {
    // The new seal, once made.
    std::optional<SecurityState> made;
    // Its seal or its write under way: the screen waits, and takes no key.
    bool waiting = true;
  };
  std::optional<Resealing> resealing_;
  // A lock asked for while the reseal waited: asked for again once it ends.
  bool lock_asked_ = false;
  // The chip lost its key while Settings checked the PIN: Pane locks, the
  // screen showing that loss for good (Paper 07.13 B).
  bool guard_lost_ = false;

  base::ObserverList<Observer> observers_;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<AppLock> weak_factory_{this};
};

}  // namespace pane::model

#endif  // PANE_MODEL_APP_LOCK_H_
