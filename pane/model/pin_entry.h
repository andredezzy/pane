// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PIN_ENTRY_H_
#define PANE_MODEL_PIN_ENTRY_H_

#include <optional>
#include <string>
#include <string_view>

namespace pane::model {

// What the PIN screen does with each key (the Electron app's
// lock-screen/page.tsx): verify the current PIN, enter a new one of at least
// four digits, confirm it. The screen draws Dots(), sends the returned action
// to the app, and reports the app's answers back.
class PinEntry {
 public:
  enum class Mode {
    kSetup,
    kChange,
    kRemove,
    // The app is locked; it cannot be dismissed.
    kUnlock,
  };

  enum class Step { kVerify, kEnter, kConfirm };

  struct Action {
    enum class Kind {
      kNone,
      // Check `pin` against the stored PIN, then call Verified().
      kVerify,
      // Store `pin` as the new PIN and close.
      kSetPin,
      // Remove the PIN (verified as `pin`) and close.
      kRemovePin,
      // Unlock the app.
      kUnlock,
      // The last attempt failed: erase everything.
      kWipe,
      // Shake the dots, then call Shook().
      kShake,
    };

    friend bool operator==(const Action&, const Action&) = default;

    Kind kind = Kind::kNone;
    // The digits, for kVerify, kSetPin, and kRemovePin.
    std::string pin;
  };

  // `pin_length` is the stored PIN's digits, none while setting one up.
  PinEntry(Mode mode, int pin_length);
  PinEntry(const PinEntry&);
  PinEntry& operator=(const PinEntry&);
  ~PinEntry();

  // Why the lock can't open: each its own state, for the PIN screen's words
  // (Paper 07.6 to 07.8), whatever words it shows today.
  enum class UnreadableReason {
    // No key the secret opens: another secret, or the files lost or damaged
    // with no copy (07.6).
    kKey,
    // The PIN's guard lost its key: deleted, or the chip cleared. What it
    // guarded is lost, as with a forgotten PIN (07.6's words until Paper's
    // 07.6.1).
    kGuardGone,
    // The PIN's guard has no chip now, or its provider refuses: what it
    // guards opens once the chip is back (07.6's words until 07.6.2).
    kGuardUnavailable,
    // The Keychain gave no secret: the user denied it, or it failed (07.7).
    kKeychainDenied,
    // DPAPI gave no secret on Windows (07.7's words until 07.7.1).
    kDpapiRefused,
    // The key opened, and the data would not: the volume's attach failed
    // (07.8).
    kDataWontOpen,
  };

  // The lock with no PIN to check against: it says why, and takes no keys.
  static PinEntry Unreadable(UnreadableReason reason, std::string message);

  Mode mode() const { return mode_; }
  Step step() const { return step_; }
  const std::string& entered() const { return entered_; }
  const std::optional<std::string>& error() const { return error_; }
  // A check is running, or the dots are shaking: keys are ignored.
  bool busy() const { return busy_; }
  // No key to check a PIN against: the dots and the numpad show at 40%
  // (Paper 07.6).
  bool is_unreadable() const { return is_unreadable_; }
  // Why, while unreadable.
  std::optional<UnreadableReason> unreadable_reason() const {
    return unreadable_reason_;
  }
  bool dismissable() const { return mode_ != Mode::kUnlock; }

  // None on the lock.
  std::optional<std::string_view> Title() const;
  int Dots() const;
  // A new PIN is long enough to continue with.
  bool CanContinue() const;

  Action Digit(char16_t digit);
  void Backspace();
  // "Continue with n-digit PIN".
  void ContinueWithPin();
  // The app's answer to kVerify.
  Action Verified(bool success, int remaining);
  // The dots stopped shaking: start over.
  void Shook();

 private:
  // The digits that submit the step, none while entering a new PIN.
  std::optional<int> SubmitLength() const;

  Mode mode_;
  int pin_length_;
  Step step_;
  std::string entered_;
  std::string first_pin_;
  std::optional<std::string> error_;
  bool busy_ = false;
  bool is_unreadable_ = false;
  std::optional<UnreadableReason> unreadable_reason_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_PIN_ENTRY_H_
