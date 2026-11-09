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

std::optional<Key> SealingKey(base::span<const uint8_t> secret,
                              std::optional<std::string_view> pin,
                              const std::optional<Stretch>& stretch) {
  std::optional<Key> salt;
  if (pin && stretch) {
    salt = Stretched(*pin, *stretch);
    if (!salt) {
      return std::nullopt;
    }
  }
  return Hkdf(
      secret,
      salt ? base::span<const uint8_t>(*salt) : base::span<const uint8_t>(),
      "Pane data key");
}

std::string Passphrase(const Key& data_key, std::string_view label) {
  return base::Base64Encode(Hkdf(data_key, {}, label));
}

}  // namespace

SealedKey::SealedKey() = default;

SealedKey::SealedKey(std::string box,
                     std::optional<Stretch> pin,
                     std::string secret)
    : box(std::move(box)), pin(std::move(pin)), secret(std::move(secret)) {}

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
  return SealedKey(*box, std::move(stretch), *secret);
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
  return value;
}

Key MakeDataKey() {
  return crypto::RandBytesAsArray<kKeySize>();
}

SealedKey SealDataKey(const Key& key,
                      base::span<const uint8_t> secret,
                      std::optional<std::string_view> pin) {
  std::optional<Stretch> stretch;
  if (pin) {
    stretch = Stretch{
        .salt = base::Base64Encode(crypto::RandBytesAsArray<kSaltSize>()),
        .rounds = kPinRounds};
  }
  // A salt just made and the rounds above always stretch.
  const Key sealing = SealingKey(secret, pin, stretch).value();
  const auto nonce = crypto::RandBytesAsArray<kNonceSize>();
  std::vector<uint8_t> combined(nonce.begin(), nonce.end());
  const std::vector<uint8_t> sealed =
      crypto::aead::Seal(crypto::aead::AES_256_GCM, sealing, key, nonce, {});
  combined.insert(combined.end(), sealed.begin(), sealed.end());
  return SealedKey(base::Base64Encode(combined), std::move(stretch),
                   Check(secret));
}

base::expected<Key, OpenError> OpenDataKey(
    const SealedKey& sealed,
    base::span<const uint8_t> secret,
    std::optional<std::string_view> pin) {
  if (sealed.secret != Check(secret)) {
    return base::unexpected(OpenError::kOtherSecret);
  }
  if (sealed.pin.has_value() != pin.has_value()) {
    return base::unexpected(OpenError::kRefused);
  }
  const std::optional<std::vector<uint8_t>> combined =
      base::Base64Decode(sealed.box);
  const std::optional<Key> sealing = SealingKey(secret, pin, sealed.pin);
  if (!combined || combined->size() < kNonceSize || !sealing) {
    return base::unexpected(OpenError::kRefused);
  }
  const auto [nonce, ciphertext] = base::span(*combined).split_at(kNonceSize);
  const std::optional<std::vector<uint8_t>> opened = crypto::aead::Open(
      crypto::aead::AES_256_GCM, *sealing, ciphertext, nonce, {});
  if (!opened || opened->size() != kKeySize) {
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
