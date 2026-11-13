// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_DATA_KEY_H_
#define PANE_MODEL_DATA_KEY_H_

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/types/expected.h"
#include "base/values.h"

namespace pane::model {

// The data key (ADR 0113): 256 random bits that every profile's data is
// encrypted under, through a key per use. It is kept only sealed, with
// AES-GCM, under a key HKDF-SHA256 derives from the engine's "Pane Safe
// Storage" secret, which the engine reads at every launch anyway. With a PIN,
// the PIN stretched with PBKDF2 is that derivation's salt, so the key opens
// only with both, and opening it is how the PIN is checked.
inline constexpr size_t kKeySize = 32;
using Key = std::array<uint8_t, kKeySize>;

// OWASP's rounds for PBKDF2-HMAC-SHA256: 64 ms on an M5 Pro, where the bcrypt
// it replaces (cost 10) took 40.
inline constexpr int kPinRounds = 600'000;

// How the PIN was stretched.
struct Stretch {
  // 16 random bytes, in base64.
  std::string salt;
  int rounds = 0;

  friend bool operator==(const Stretch&, const Stretch&) = default;
};

// The data key, sealed, as security.json keeps it under "key".
struct SealedKey {
  SealedKey();
  SealedKey(std::string box, std::optional<Stretch> pin, std::string secret);
  SealedKey(const SealedKey&);
  SealedKey& operator=(const SealedKey&);
  ~SealedKey();

  // nullopt for a value that isn't one: a missing or mistyped field.
  static std::optional<SealedKey> FromValue(const base::DictValue& value);
  base::DictValue ToValue() const;

  friend bool operator==(const SealedKey&, const SealedKey&) = default;

  // AES-GCM's combined box (nonce, ciphertext, and tag), in base64.
  std::string box;
  // How the PIN was stretched; nullopt when it was sealed without one.
  std::optional<Stretch> pin;
  // Tells another secret from a wrong PIN: without it, an engine key replaced
  // would make every PIN look wrong, and the fifth would wipe.
  std::string secret;
};

enum class OpenError {
  // The secret is not the one the key was sealed under: not the PIN's fault,
  // so no attempt counts.
  kOtherSecret,
  // Another PIN, a box changed since it was sealed, or a PIN for a key sealed
  // without one, or none for a key sealed with one.
  kRefused,
};

// AES-GCM's combined box, as CryptoKit seals one: the nonce, the ciphertext,
// and the tag, in base64. What Pane seals is one: the data key, a sealed
// file, the proxy passwords. `aad` is bound to it, unencrypted.
std::string SealBox(base::span<const uint8_t> key,
                    base::span<const uint8_t> plaintext,
                    base::span<const uint8_t> aad = {});

// Why a box doesn't open: it isn't one (not base64, or shorter than its
// nonce), or it isn't sealed under this key and `aad`.
enum class BoxError { kMalformed, kRefused };

base::expected<std::vector<uint8_t>, BoxError> OpenBox(
    base::span<const uint8_t> key,
    std::string_view box,
    base::span<const uint8_t> aad = {});

Key MakeDataKey();

// `secret` is the Keychain item's stored bytes, its base64 text as it is, not
// decoded. Each seal stretches the PIN with a salt of its own.
SealedKey SealDataKey(const Key& key,
                      base::span<const uint8_t> secret,
                      std::optional<std::string_view> pin);

base::expected<Key, OpenError> OpenDataKey(const SealedKey& sealed,
                                           base::span<const uint8_t> secret,
                                           std::optional<std::string_view> pin);

// Pane's own files, sealed in place (SealedFile).
Key FilesKey(const Key& data_key);

// The key every file a profile's engine writes is encrypted under (ADR 0121),
// which only Pane's processes hold in memory.
Key EngineFilesKey(const Key& data_key, std::string_view profile_id);

// The volume's passphrase, which diskutil reads from standard input.
std::string VolumePassphrase(const Key& data_key);

// What a Pane before ADR 0121 derives. The first open of a volume that still
// takes it changes it to the one above before any file converts, so an older
// Pane, whose engine would raze encrypted stores, opens nothing.
std::string FormerVolumePassphrase(const Key& data_key);

}  // namespace pane::model

#endif  // PANE_MODEL_DATA_KEY_H_
