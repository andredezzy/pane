// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/data_key.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "crypto/aead.h"
#include "crypto/hash.h"
#include "crypto/hmac.h"
#include "crypto/kdf.h"
#include "crypto/random.h"
#include "third_party/boringssl/src/include/openssl/evp.h"

namespace pane::model {

namespace {

// AES-GCM's, as CryptoKit seals.
constexpr size_t kNonceSize = 12;
constexpr size_t kSaltSize = 16;
// Of the secret's HMAC, what is kept.
constexpr size_t kCheckSize = 16;

Key Hkdf(base::span<const uint8_t> secret,
         base::span<const uint8_t> salt,
         std::string_view info) {
  return crypto::kdf::Hkdf<kKeySize>(crypto::hash::kSha256, secret, salt,
                                     base::as_byte_span(info));
}

// HMAC-SHA256 of the secret, cut to 16 bytes, in base64. The secret is 128
// random bits: no one finds it from this.
std::string Check(base::span<const uint8_t> secret) {
  const auto mac = crypto::hmac::SignSha256(
      secret, base::as_byte_span(std::string_view("Pane data key secret")));
  return base::Base64Encode(base::span(mac).first<kCheckSize>());
}

// PBKDF2-HMAC-SHA256 of the PIN's UTF-8 digits, 32 bytes.
std::optional<Key> Stretched(std::string_view pin, const Stretch& stretch) {
  const std::optional<std::vector<uint8_t>> salt =
      base::Base64Decode(stretch.salt);
  if (!salt || stretch.rounds <= 0) {
    return std::nullopt;
  }
  Key stretched;
  if (!PKCS5_PBKDF2_HMAC(pin.data(), pin.size(), salt->data(), salt->size(),
                         static_cast<uint32_t>(stretch.rounds), EVP_sha256(),
                         stretched.size(), stretched.data())) {
    return std::nullopt;
  }
  return stretched;
}

// With a guard's share, the derivation takes the secret and the share, under
// an info of its own: nothing on disk alone gives it.
std::optional<Key> SealingKey(base::span<const uint8_t> secret,
                              std::optional<std::string_view> pin,
                              const std::optional<Stretch>& stretch,
                              const std::optional<Key>& share = std::nullopt) {
  std::optional<Key> salt;
  if (pin && stretch) {
    salt = Stretched(*pin, *stretch);
    if (!salt) {
      return std::nullopt;
    }
  }
  const base::span<const uint8_t> salt_bytes =
      salt ? base::span<const uint8_t>(*salt) : base::span<const uint8_t>();
  if (!share) {
    return Hkdf(secret, salt_bytes, "Pane data key");
  }
  std::vector<uint8_t> material(secret.begin(), secret.end());
  material.insert(material.end(), share->begin(), share->end());
  return Hkdf(material, salt_bytes, "Pane data key, its PIN guarded");
}

std::string Passphrase(const Key& data_key, std::string_view label) {
  return base::Base64Encode(Hkdf(data_key, {}, label));
}

}  // namespace

SealedKey::SealedKey() = default;

SealedKey::SealedKey(std::string box,
                     std::optional<Stretch> pin,
                     std::string secret,
                     std::optional<GuardedShare> guard)
    : box(std::move(box)),
      pin(std::move(pin)),
      secret(std::move(secret)),
      guard(std::move(guard)) {}

SealedKey::SealedKey(const SealedKey&) = default;
SealedKey& SealedKey::operator=(const SealedKey&) = default;
SealedKey::~SealedKey() = default;

// static
std::optional<SealedKey> SealedKey::FromValue(const base::DictValue& value) {
  const std::string* box = value.FindString("box");
  const std::string* secret = value.FindString("secret");
  if (!box || !secret) {
    return std::nullopt;
  }
  std::optional<Stretch> stretch;
  if (const base::Value* pin = value.Find("pin"); pin && !pin->is_none()) {
    const base::DictValue* dict = pin->GetIfDict();
    const std::string* salt = dict ? dict->FindString("salt") : nullptr;
    const std::optional<int> rounds =
        dict ? dict->FindInt("rounds") : std::nullopt;
    if (!salt || !rounds) {
      return std::nullopt;
    }
    stretch = Stretch{.salt = *salt, .rounds = *rounds};
  }
  std::optional<GuardedShare> guard;
  if (const base::Value* tpm = value.Find("tpm"); tpm && !tpm->is_none()) {
    const base::DictValue* dict = tpm->GetIfDict();
    const std::string* key = dict ? dict->FindString("key") : nullptr;
    const std::string* share = dict ? dict->FindString("share") : nullptr;
    if (!key || !share) {
      return std::nullopt;
    }
    guard = GuardedShare{.key = *key, .share = *share};
  }
  return SealedKey(*box, std::move(stretch), *secret, std::move(guard));
}

base::DictValue SealedKey::ToValue() const {
  base::DictValue value;
  value.Set("box", box);
  // Absent without a PIN, as Swift's synthesized coding leaves it.
  if (pin) {
    value.Set(
        "pin",
        base::DictValue().Set("rounds", pin->rounds).Set("salt", pin->salt));
  }
  value.Set("secret", secret);
  // Absent without a guard: every seal of the Mac's.
  if (guard) {
    value.Set(
        "tpm",
        base::DictValue().Set("key", guard->key).Set("share", guard->share));
  }
  return value;
}

std::string SealBox(base::span<const uint8_t> key,
                    base::span<const uint8_t> plaintext,
                    base::span<const uint8_t> aad) {
  const auto nonce = crypto::RandBytesAsArray<kNonceSize>();
  std::vector<uint8_t> combined(nonce.begin(), nonce.end());
  const std::vector<uint8_t> sealed =
      crypto::aead::Seal(crypto::aead::AES_256_GCM, key, plaintext, nonce, aad);
  combined.insert(combined.end(), sealed.begin(), sealed.end());
  return base::Base64Encode(combined);
}

base::expected<std::vector<uint8_t>, BoxError> OpenBox(
    base::span<const uint8_t> key,
    std::string_view box,
    base::span<const uint8_t> aad) {
  const std::optional<std::vector<uint8_t>> combined = base::Base64Decode(box);
  if (!combined || combined->size() < kNonceSize) {
    return base::unexpected(BoxError::kMalformed);
  }
  const auto [nonce, ciphertext] = base::span(*combined).split_at(kNonceSize);
  std::optional<std::vector<uint8_t>> opened = crypto::aead::Open(
      crypto::aead::AES_256_GCM, key, ciphertext, nonce, aad);
  if (!opened) {
    return base::unexpected(BoxError::kRefused);
  }
  return std::move(*opened);
}

Key MakeDataKey() {
  return crypto::RandBytesAsArray<kKeySize>();
}

SealedKey SealDataKey(const Key& key,
                      base::span<const uint8_t> secret,
                      std::optional<std::string_view> pin,
                      const std::optional<Guarded>& guarded) {
  std::optional<Stretch> stretch;
  if (pin) {
    stretch = Stretch{
        .salt = base::Base64Encode(crypto::RandBytesAsArray<kSaltSize>()),
        .rounds = kPinRounds};
  }
  std::optional<Key> share;
  std::optional<GuardedShare> guard;
  if (guarded) {
    share = guarded->share;
    guard = guarded->kept;
  }
  // A salt just made and the rounds above always stretch.
  const Key sealing = SealingKey(secret, pin, stretch, share).value();
  return SealedKey(SealBox(sealing, key), std::move(stretch), Check(secret),
                   std::move(guard));
}

bool IsSealedUnder(const SealedKey& sealed, base::span<const uint8_t> secret) {
  return sealed.secret == Check(secret);
}

base::expected<Key, OpenError> OpenDataKey(const SealedKey& sealed,
                                           base::span<const uint8_t> secret,
                                           std::optional<std::string_view> pin,
                                           const std::optional<Key>& share) {
  if (!IsSealedUnder(sealed, secret)) {
    return base::unexpected(OpenError::kOtherSecret);
  }
  if (sealed.pin.has_value() != pin.has_value() ||
      sealed.guard.has_value() != share.has_value()) {
    return base::unexpected(OpenError::kRefused);
  }
  const std::optional<Key> sealing = SealingKey(secret, pin, sealed.pin, share);
  if (!sealing) {
    return base::unexpected(OpenError::kRefused);
  }
  const base::expected<std::vector<uint8_t>, BoxError> opened =
      OpenBox(*sealing, sealed.box);
  if (!opened.has_value() || opened->size() != kKeySize) {
    return base::unexpected(OpenError::kRefused);
  }
  Key key;
  std::ranges::copy(*opened, key.begin());
  return key;
}

Key FilesKey(const Key& data_key) {
  return Hkdf(data_key, {}, "Pane files");
}

Key EngineFilesKey(const Key& data_key, std::string_view profile_id) {
  return Hkdf(data_key, base::as_byte_span(profile_id), "Pane engine files");
}

std::string VolumePassphrase(const Key& data_key) {
  return Passphrase(data_key, "Pane data volume, files encrypted");
}

std::string FormerVolumePassphrase(const Key& data_key) {
  return Passphrase(data_key, "Pane data volume");
}

}  // namespace pane::model
