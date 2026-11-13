// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/app_lock.h"

#include <string_view>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "pane/model/persisted_file.h"

namespace pane::model {

namespace {

constexpr char kSecurityName[] = "security";
// security.json's copy beside the volume: losing both loses the data.
constexpr char kCopyName[] = "Data.security";

// PBKDF2 and bcrypt take a moment, and the Keychain may ask the user first.
constexpr base::TaskTraits kKeyTraits = {
    base::MayBlock(), base::TaskPriority::USER_BLOCKING,
    base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN};

std::optional<std::string_view> ViewOf(const std::optional<std::string>& pin) {
  if (!pin) {
    return std::nullopt;
  }
  return *pin;
}

// What the PIN's guard gave back for a guarded seal, or why it gave nothing.
base::expected<std::optional<Key>, AppLock::Opened::Kind> ReleasedShare(
    const SealedKey& sealed,
    const std::optional<std::string>& pin,
    const scoped_refptr<PinGuard>& guard) {
  // A seal without a guard, or the launch's open without a PIN, which the
  // seal refuses on its own.
  if (!sealed.guard || !pin) {
    return std::nullopt;
  }
  if (!guard) {
    return base::unexpected(AppLock::Opened::Kind::kGuardGone);
  }
  base::expected<Key, PinGuard::Refusal> released =
      guard->Unseal(*sealed.guard, *pin);
  if (released.has_value()) {
    return std::make_optional(*released);
  }
  switch (released.error()) {
    case PinGuard::Refusal::kWrongPin:
      return base::unexpected(AppLock::Opened::Kind::kWrongPin);
    case PinGuard::Refusal::kLockedOut:
      return base::unexpected(AppLock::Opened::Kind::kLockedOut);
    case PinGuard::Refusal::kGone:
    case PinGuard::Refusal::kUnavailable:
      return base::unexpected(AppLock::Opened::Kind::kGuardGone);
  }
}

// Opens the data key with the PIN, or, before any is sealed, makes it: sealed
// with the PIN, which matches the bcrypt hash an earlier Pane kept. `known`:
// the secret this session read already, so Change and Remove, and an unlock
// after a lock, ask the Keychain nothing more. `makes_secret`: whether the
// Keychain's secret may be made here when none is found. `attach`: the key
// opens the data, and its volume starts unlocking at once, from here
// (MakeAttacher). A guarded seal asks its guard only once the secret is known
// to be the seal's: another secret counts no attempt, the chip's included.
AppLock::Opened OpenKey(std::optional<std::string> pin,
                        SecurityState state,
                        std::optional<std::vector<uint8_t>> known,
                        bool makes_secret,
                        AppLock::System system,
                        DataVault::Attacher attach) {
  AppLock::Opened opened;
  std::optional<std::vector<uint8_t>> secret =
      known ? std::move(known)
            : system.secret.Run(/*makes_one=*/makes_secret && !state.key);
  if (!secret) {
    opened.kind = AppLock::Opened::Kind::kNoSecret;
    return opened;
  }

  if (state.key) {
    if (!IsSealedUnder(*state.key, *secret)) {
      opened.kind = AppLock::Opened::Kind::kOtherSecret;
      return opened;
    }
    const base::expected<std::optional<Key>, AppLock::Opened::Kind> share =
        ReleasedShare(*state.key, pin, system.guard);
    if (!share.has_value()) {
      opened.kind = share.error();
      return opened;
    }
    const base::expected<Key, OpenError> key =
        OpenDataKey(*state.key, *secret, ViewOf(pin), *share);
    if (!key.has_value()) {
      opened.kind = key.error() == OpenError::kOtherSecret
                        ? AppLock::Opened::Kind::kOtherSecret
                        : AppLock::Opened::Kind::kWrongPin;
      return opened;
    }
    if (attach) {
      std::move(attach).Run(*key);
    }
    opened.kind = AppLock::Opened::Kind::kKey;
    opened.key = *key;
    opened.secret = std::move(*secret);
    return opened;
  }

  if (pin && (!state.pin || !state.pin->hash || !system.matches_hash ||
              !system.matches_hash.Run(*pin, *state.pin->hash))) {
    opened.kind = AppLock::Opened::Kind::kWrongPin;
    return opened;
  }

  // Saved first (OnOpened): a quit before would leave a volume made with no
  // key saved to open it, which Pane takes for a key lost.
  const Key key = MakeDataKey();
  opened.kind = AppLock::Opened::Kind::kKey;
  opened.made = SealDataKey(key, *secret, ViewOf(pin));
  opened.key = key;
  opened.secret = std::move(*secret);
  return opened;
}

// security.json, and its copy beside the volume.
void WriteSecurityFiles(const base::FilePath& data_directory,
                        base::Value state) {
  for (const char* name : {kSecurityName, kCopyName}) {
    if (!PersistedFile(data_directory, name).Save(state.Clone())) {
      LOG(ERROR) << "Could not save " << name << ".json";
    }
  }
}

}  // namespace

AppLock::System::System() = default;
AppLock::System::System(const System&) = default;
AppLock::System& AppLock::System::operator=(const System&) = default;
AppLock::System::~System() = default;

AppLock::Opened::Opened() = default;
AppLock::Opened::Opened(const Opened&) = default;
AppLock::Opened& AppLock::Opened::operator=(const Opened&) = default;
AppLock::Opened::~Opened() = default;

// static
AppLock::Loaded AppLock::Load(const base::FilePath& data_directory,
                              bool holds_data) {
  const PersistedFile file(data_directory, kSecurityName);
  Loaded loaded;

  std::optional<SecurityState> state;
  if (const base::expected<std::optional<base::Value>, FileError> read =
          file.Load();
      read.has_value()) {
    state = *read ? SecurityState::FromValue(**read)
                  : std::make_optional<SecurityState>();
  }
  if (state) {
    loaded.state = std::move(*state);
  } else {
    LOG(ERROR) << "Could not read security.json";
    KeepDamaged(file.path());
    std::string text;
    std::optional<SecurityState> recovered =
        base::ReadFileToString(file.path(), &text)
            ? SecurityState::Recover(text)
            : std::nullopt;
    if (recovered) {
      loaded.state = std::move(*recovered);
    } else {
      loaded.unreadable = true;
    }
  }

  // The copy stands in for a key security.json lost: the file removed, or no
  // longer decoding.
  if (!loaded.state.key) {
    const base::expected<std::optional<base::Value>, FileError> kept =
        PersistedFile(data_directory, kCopyName).Load();
    std::optional<SecurityState> copy = kept.has_value() && *kept
                                            ? SecurityState::FromValue(**kept)
                                            : std::nullopt;
    if (copy && copy->key) {
      loaded.state = std::move(*copy);
      loaded.unreadable = false;
    }
  }

  // The lock's fifth attempt takes the key in the same save, so the last
  // attempt with a key still there came from Settings in 0.18.0, where wrong
  // PINs counted: it leaves the lock its attempts, or one typo would erase.
  if (loaded.state.failed_attempts >= SecurityState::kMaxAttempts &&
      loaded.state.key) {
    loaded.state.failed_attempts = 0;
  }

  // Data no key opens: what it holds cannot be read.
  loaded.key_lost = !loaded.state.key && holds_data;
  return loaded;
}

AppLock::AppLock(const base::FilePath& data_directory,
                 Loaded loaded,
                 DataVault& vault,
                 System system,
                 Delegate* delegate,
                 std::optional<LaunchSecret> launch_secret)
    : data_directory_(data_directory),
      vault_(vault),
      system_(std::move(system)),
      delegate_(delegate),
      makes_secret_(!launch_secret),
      files_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
      state_(std::move(loaded.state)),
      is_locked_(state_.pin || loaded.unreadable || loaded.key_lost),
      secret_(launch_secret ? std::move(launch_secret->secret) : std::nullopt) {
  CHECK(delegate_);
  if (loaded.unreadable || loaded.key_lost) {
    entry_ = PinEntry::Unreadable(kKeyUnreadable);
  } else if (state_.pin) {
    entry_ = PinEntry(PinEntry::Mode::kUnlock, state_.pin->length);
  }
}

AppLock::~AppLock() = default;

void AppLock::AddObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void AppLock::RemoveObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

void AppLock::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_locked_) {
    // Found now, and never made here: the PIN's open makes one if it must.
    if (entry_ && !entry_->is_unreadable() && !secret_) {
      finding_secret_ = true;
      base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE, kKeyTraits,
          base::BindOnce(system_.secret, /*makes_one=*/false),
          base::BindOnce(&AppLock::SecretFound, weak_factory_.GetWeakPtr()));
    }
    return;
  }
  PostOpen(std::nullopt, /*unlocking=*/true);
}

void AppLock::SecretFound(std::optional<std::vector<uint8_t>> secret) {
  finding_secret_ = false;
  if (!secret_ && secret) {
    secret_ = std::move(secret);
  }
  if (std::optional<WaitingOpen> open = std::exchange(waiting_open_, {})) {
    PostOpen(std::move(open->pin), open->unlocking);
  }
}

void AppLock::LockNow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!state_.pin || is_locked_) {
    return;
  }
  is_locked_ = true;
  entry_ = PinEntry(PinEntry::Mode::kUnlock, state_.pin->length);
  Changed();
}

void AppLock::VolumeWontOpen() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_locked_ = true;
  entry_ = PinEntry::Unreadable(kDataWontOpen);
  Changed();
}

void AppLock::Show(PinEntry::Mode mode) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_locked_ || mode == PinEntry::Mode::kUnlock) {
    return;
  }
  entry_ = PinEntry(mode, state_.pin ? state_.pin->length : 0);
  Changed();
}

void AppLock::Dismiss() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!entry_ || !entry_->dismissable()) {
    return;
  }
  entry_.reset();
  Changed();
}

void AppLock::Digit(char16_t digit) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!entry_) {
    return;
  }
  const PinEntry::Action action = entry_->Digit(digit);
  Changed();
  Perform(action);
}

void AppLock::Backspace() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (entry_) {
    entry_->Backspace();
    Changed();
  }
}

void AppLock::ContinueWithPin() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (entry_) {
    entry_->ContinueWithPin();
    Changed();
  }
}

void AppLock::SetWipeForTesting(base::RepeatingClosure wipe) {
  wipe_for_testing_ = std::move(wipe);
}

void AppLock::Perform(PinEntry::Action action) {
  switch (action.kind) {
    case PinEntry::Action::Kind::kNone:
      return;
    case PinEntry::Action::Kind::kVerify:
      Verify(action.pin);
      return;
    case PinEntry::Action::Kind::kSetPin:
      Reseal(action.pin);
      return;
    case PinEntry::Action::Kind::kRemovePin:
      Reseal(std::nullopt);
      return;
    case PinEntry::Action::Kind::kUnlock:
      is_locked_ = false;
      entry_.reset();
      Changed();
      delegate_->DidUnlock();
      return;
    case PinEntry::Action::Kind::kWipe:
      Wipe();
      return;
    case PinEntry::Action::Kind::kShake:
      for (Observer& observer : observers_) {
        observer.OnPinShake();
      }
      // The dots shake for 0.4 s; the entry starts over at 0.6 s.
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&AppLock::Shook, weak_factory_.GetWeakPtr()),
          base::Milliseconds(600));
      return;
  }
}

void AppLock::Shook() {
  if (entry_) {
    entry_->Shook();
    Changed();
  }
}

// A wrong PIN at the lock counts at once, on disk too, so quitting cannot
// reset the count. A key that cannot be read at all is not the PIN's fault,
// and counts nothing.
void AppLock::Verify(const std::string& pin) {
  const bool unlocking = entry_ && entry_->mode() == PinEntry::Mode::kUnlock;
  // The launch's read is on its way: the open takes its secret when it comes.
  if (finding_secret_ && !secret_) {
    waiting_open_ = WaitingOpen{pin, unlocking};
    return;
  }
  PostOpen(pin, unlocking);
}

void AppLock::PostOpen(std::optional<std::string> pin, bool unlocking) {
  const bool with_pin = pin.has_value();
  DataVault::Attacher attach =
      unlocking ? vault_->MakeAttacher() : DataVault::Attacher();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kKeyTraits,
      base::BindOnce(&OpenKey, std::move(pin), state_, secret_, makes_secret_,
                     system_, std::move(attach)),
      base::BindOnce(&AppLock::OnOpened, weak_factory_.GetWeakPtr(), unlocking,
                     with_pin));
}

void AppLock::OnOpened(bool unlocking, bool with_pin, Opened opened) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  switch (opened.kind) {
    case Opened::Kind::kNoSecret:
      is_locked_ = true;
      entry_ = PinEntry::Unreadable(kKeychainDenied);
      Changed();
      return;

    case Opened::Kind::kOtherSecret:
      is_locked_ = true;
      entry_ = PinEntry::Unreadable(kKeyUnreadable);
      Changed();
      return;

    // The TPM's key gone, the chip cleared, or none for a seal that took one:
    // the data behind the PIN is lost, as with a forgotten PIN, and the lock
    // shows an unreadable key (Paper 07.6, in the Mac's words; Windows' are
    // W5's).
    case Opened::Kind::kGuardGone:
      is_locked_ = true;
      entry_ = PinEntry::Unreadable(kKeyUnreadable);
      Changed();
      return;

    // The chip's lockout refuses every PIN, the right one too: none counts,
    // at the lock or in Settings, and the dots shake (its words are W5's).
    case Opened::Kind::kLockedOut:
      Verified(false);
      return;

    case Opened::Kind::kWrongPin:
      // The launch's open, without a PIN: a key sealed with a PIN
      // security.json no longer names. The Swift told it by the PIN screen
      // being gone, which a Settings sheet dismissed while its PIN was
      // checked is too, and shut the lock for good on a wrong PIN there.
      if (!with_pin) {
        is_locked_ = true;
        entry_ = PinEntry::Unreadable(kKeyUnreadable);
        Changed();
        return;
      }
      if (unlocking) {
        ++state_.failed_attempts;
        // The last attempt and the key go in one save: with the key gone,
        // nothing left opens the volume or the sealed files, wherever their
        // blocks are (ADR 0113), and a wipe is due (IsWipeDue). The answer,
        // the shake or the wipe, comes once it is on the disk. The PIN's
        // guard goes before that save: a copy of security.json made earlier
        // opens nothing once the TPM's key is gone.
        base::OnceClosure answer =
            base::BindOnce(&AppLock::SaveThen, weak_factory_.GetWeakPtr(),
                           base::BindOnce(&AppLock::Verified,
                                          weak_factory_.GetWeakPtr(), false));
        if (state_.failed_attempts >= SecurityState::kMaxAttempts) {
          state_.key.reset();
          ForgetGuardThen(std::nullopt, std::move(answer));
          return;
        }
        std::move(answer).Run();
        return;
      }
      ++settings_attempts_;
      Verified(false);
      if (settings_attempts_ >= SecurityState::kMaxAttempts) {
        settings_attempts_ = 0;
        delegate_->DidExhaustSettingsAttempts();
      }
      return;

    case Opened::Kind::kKey:
      state_.failed_attempts = 0;
      settings_attempts_ = 0;
      if (opened.made) {
        state_.key = std::move(*opened.made);
        if (state_.pin) {
          state_.pin->hash.reset();
        }
      }
      if (unlocking) {
        secret_ = std::move(opened.secret);
        if (opened.made) {
          // A key just made is saved before the volume it opens is made.
          SaveThen(base::BindOnce(&AppLock::OpenVault,
                                  weak_factory_.GetWeakPtr(), opened.key));
        } else {
          Save();
          vault_->Open(opened.key);
        }
      } else {
        Save();
      }
      Verified(true);
      return;
  }
}

void AppLock::OpenVault(const Key& key) {
  vault_->MakeAttacher().Run(key);
  vault_->Open(key);
}

void AppLock::Verified(bool matches) {
  if (!entry_) {
    return;
  }
  const PinEntry::Action action =
      entry_->Verified(matches, state_.remaining_attempts());
  Changed();
  Perform(action);
}

// Seals the open key again: with a new PIN (set up, changed), or none
// (removed). Only the key's seal changes; the data stays as it is.
void AppLock::Reseal(std::optional<std::string> pin) {
  const std::optional<Key>& key = vault_->data_key();
  if (!key || !secret_) {
    LOG(ERROR) << "No open data key to seal again";
    return;
  }
  std::optional<int> length;
  if (pin) {
    length = static_cast<int>(pin->size());
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kKeyTraits,
      base::BindOnce(
          [](const Key& key, const std::vector<uint8_t>& secret,
             const std::optional<std::string>& pin,
             const scoped_refptr<PinGuard>& guard) -> std::optional<SealedKey> {
            // With a PIN, the guard takes part when there is one, and a new
            // key the chip holds has the new PIN for its own. Without a chip,
            // or with the provider refusing, the PIN's stretch alone; but not
            // in a lockout, which four wrong PINs bring about: the earlier
            // seal stays.
            std::optional<Guarded> guarded;
            if (pin && guard) {
              base::expected<Guarded, PinGuard::Refusal> made =
                  guard->Guard(*pin);
              if (made.has_value()) {
                guarded = std::move(*made);
              } else if (made.error() == PinGuard::Refusal::kLockedOut) {
                return std::nullopt;
              } else {
                LOG(ERROR) << "The PIN is sealed without the TPM";
              }
            }
            return SealDataKey(key, secret, ViewOf(pin), guarded);
          },
          *key, *secret_, std::move(pin), system_.guard),
      base::BindOnce(&AppLock::Resealed, weak_factory_.GetWeakPtr(), length));
}

void AppLock::Resealed(std::optional<int> length,
                       std::optional<SealedKey> sealed) {
  // The lockout made no key: nothing changes, and the dots shake (its words
  // are W5's).
  if (!sealed) {
    Verified(false);
    return;
  }
  std::optional<Pin> pin;
  if (length) {
    pin = Pin{.length = *length};
  }
  std::optional<std::string> kept;
  if (sealed->guard) {
    kept = sealed->guard->key;
  }
  state_ = SecurityState(std::move(pin), 0, std::move(*sealed));
  // The guard's earlier keys go once the new seal is on the disk: a quit
  // before leaves the earlier seal, which they still open.
  SaveThen(base::BindOnce(&AppLock::ForgetGuardThen, weak_factory_.GetWeakPtr(),
                          std::move(kept), base::DoNothing()));
  entry_.reset();
  Changed();
}

// The guard's keys of this data's, but `kept`, deleted off this sequence,
// then `then`.
void AppLock::ForgetGuardThen(std::optional<std::string> kept,
                              base::OnceClosure then) {
  if (!system_.guard) {
    std::move(then).Run();
    return;
  }
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE, kKeyTraits,
      base::BindOnce(
          [](scoped_refptr<PinGuard> guard, std::optional<std::string> kept) {
            guard->Forget(kept ? std::make_optional<std::string_view>(*kept)
                               : std::nullopt);
          },
          system_.guard, std::move(kept)),
      std::move(then));
}

void AppLock::Save() {
  SaveThen(base::DoNothing());
}

void AppLock::SaveThen(base::OnceClosure then) {
  files_->PostTaskAndReply(
      FROM_HERE,
      base::BindOnce(&WriteSecurityFiles, data_directory_, state_.ToValue()),
      std::move(then));
}

// Only after the last attempt at the lock: the count lives on disk, where
// nothing but a wrong PIN there raises it, and it is there by now.
void AppLock::Wipe() {
  if (!state_.pin || state_.failed_attempts < SecurityState::kMaxAttempts) {
    return;
  }
  if (wipe_for_testing_) {
    wipe_for_testing_.Run();
    return;
  }
  base::ThreadPool::PostTask(FROM_HERE, kKeyTraits, system_.wipe);
}

void AppLock::Changed() {
  for (Observer& observer : observers_) {
    observer.OnAppLockChanged();
  }
}

}  // namespace pane::model
