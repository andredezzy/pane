// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PIN_GUARD_H_
#define PANE_MODEL_PIN_GUARD_H_

#include <optional>
#include <string_view>

#include "base/memory/ref_counted.h"
#include "base/types/expected.h"
#include "pane/model/data_key.h"

namespace base {
class FilePath;
}

namespace pane::model {

// What guards the PIN in the data key's seal (U5). On Windows it is a key
// the TPM holds, through CNG's Microsoft Platform Crypto Provider, whose own
// PIN is the PIN, set silently, with no Windows UI: the seal takes a share
// only that key gives back (SealDataKey), so the chip's dictionary-attack
// lockout bounds the guesses at the PIN, and nothing on disk can be tried
// offline. DPAPI gives the secret to any process of the user's there, and a
// few digits stretched by PBKDF2 alone fall in minutes. The Mac has none: its
// Keychain asks the user before another app reads the secret (ADR 0113).
//
// Every call blocks on the chip: where blocking is allowed, never on the
// window's sequence.
class PinGuard : public base::RefCountedThreadSafe<PinGuard> {
 public:
  // Why the guard gave nothing.
  enum class Refusal {
    // Another PIN: the chip counts it toward its lockout.
    kWrongPin,
    // A lockout: Windows' own for a standard user, as Pane runs, after 4
    // failures in its window (8 hours by default), or the chip's, after 31,
    // one forgotten every 10 minutes. It takes no PIN, the right one
    // included, and makes no key.
    kLockedOut,
    // The key is gone, or the chip was cleared: what it guarded is lost, as
    // with a forgotten PIN.
    kGone,
    // No TPM 2.0, or the provider refused: a seal goes without, the PIN's
    // PBKDF2 stretch alone.
    kUnavailable,
  };

  // This system's guard for the data in `data_directory`: the TPM's on
  // Windows (pin_guard_win.cc), none on the Mac (pin_guard_mac.cc).
  static scoped_refptr<PinGuard> ForData(const base::FilePath& data_directory);

  PinGuard(const PinGuard&) = delete;
  PinGuard& operator=(const PinGuard&) = delete;

  // A new key the chip holds, `pin` its own, and a share of 32 random bytes
  // sealed under it. kLockedOut or kUnavailable when it makes none.
  virtual base::expected<Guarded, Refusal> Guard(std::string_view pin) = 0;

  // The share `kept` holds, which the chip gives back only with `pin`.
  virtual base::expected<Key, Refusal> Unseal(const GuardedShare& kept,
                                              std::string_view pin) = 0;

  // Deletes every key of this data's but `kept`: the earlier one a reseal
  // leaves, or, with none kept, all of them, as the panic wipe does. A copy
  // of security.json made before then opens nothing after.
  virtual void Forget(std::optional<std::string_view> kept) = 0;

 protected:
  friend class base::RefCountedThreadSafe<PinGuard>;

  PinGuard() = default;
  virtual ~PinGuard() = default;
};

}  // namespace pane::model

#endif  // PANE_MODEL_PIN_GUARD_H_
