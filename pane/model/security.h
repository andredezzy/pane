// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_SECURITY_H_
#define PANE_MODEL_SECURITY_H_

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include "base/values.h"
#include "pane/model/data_key.h"

namespace pane::model {

// The fewest digits a PIN takes.
inline constexpr int kMinimumPinLength = 4;

// The app lock's PIN.
struct Pin {
  // The bcrypt hash the Electron app and earlier Panes checked the PIN
  // against: checked once more at the first unlock, when the PIN seals the
  // data key and the hash goes.
  std::optional<std::string> hash;
  // The digits, so the lock screen draws that many dots and submits on the
  // last one.
  int length = 0;

  friend bool operator==(const Pin&, const Pin&) = default;
};

// security.json's state, as the Electron app persisted it (a PIN, the failed
// attempts that lead to the panic wipe), with the data key, sealed under the
// engine's secret and the PIN (ADR 0113).
struct SecurityState {
  static constexpr int kMaxAttempts = 5;

  SecurityState();
  SecurityState(std::optional<Pin> pin,
                int failed_attempts,
                std::optional<SealedKey> key);
  SecurityState(const SecurityState&);
  SecurityState& operator=(const SecurityState&);
  ~SecurityState();

  // nullopt for a state that doesn't decode: a field of the wrong type, or a
  // PIN without its length.
  static std::optional<SecurityState> FromValue(const base::Value& value);
  // The PIN as an explicit null without one, as the Electron app writes it.
  base::Value ToValue() const;

  // The state of a security.json that no longer decodes (a disk error, a
  // newer Pane's format), from what is left of it: the PIN's bcrypt hash, its
  // length, and the failed attempts. nullopt when no PIN can be read in it. A
  // sealed key is not read back from the pieces: its copy beside the volume
  // stands in (ADR 0113).
  static std::optional<SecurityState> Recover(std::string_view text);

  int remaining_attempts() const {
    return std::max(0, kMaxAttempts - failed_attempts);
  }

  friend bool operator==(const SecurityState&, const SecurityState&) = default;

  std::optional<Pin> pin;
  int failed_attempts = 0;
  // nullopt until the first launch that makes it.
  std::optional<SealedKey> key;
};

}  // namespace pane::model

#endif  // PANE_MODEL_SECURITY_H_
