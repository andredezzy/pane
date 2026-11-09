// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/proxy_password_vault.h"

#include <array>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "crypto/aead.h"
#include "crypto/hash.h"
#include "crypto/kdf.h"
#include "crypto/random.h"

namespace pane::model {

namespace {

// AES-GCM's, as CryptoKit seals.
constexpr size_t kNonceSize = 12;
constexpr size_t kKeySize = 32;

std::array<uint8_t, kKeySize> SealingKey(base::span<const uint8_t> secret) {
  return crypto::kdf::Hkdf<kKeySize>(
      crypto::hash::kSha256, secret, {},
      base::as_byte_span(std::string_view("Pane proxy passwords")));
}

}  // namespace

// static
std::optional<SealedProxyPasswords> SealedProxyPasswords::FromValue(
    const base::Value& value) {
  const std::string* box =
      value.is_dict() ? value.GetDict().FindString("box") : nullptr;
  if (!box) {
    return std::nullopt;
  }
  return SealedProxyPasswords{.box = *box};
}

base::Value SealedProxyPasswords::ToValue() const {
  return base::Value(base::DictValue().Set("box", box));
}

SealedProxyPasswords SealProxyPasswords(const ProxyPasswords& passwords,
                                        base::span<const uint8_t> secret) {
  base::DictValue dict;
  for (const auto& [id, password] : passwords) {
    dict.Set(id, password);
  }
  // A dictionary of strings always writes.
  const std::string json = base::WriteJson(dict).value();
  const auto nonce = crypto::RandBytesAsArray<kNonceSize>();
  std::vector<uint8_t> combined(nonce.begin(), nonce.end());
  const std::vector<uint8_t> sealed =
      crypto::aead::Seal(crypto::aead::AES_256_GCM, SealingKey(secret),
                         base::as_byte_span(json), nonce, {});
  combined.insert(combined.end(), sealed.begin(), sealed.end());
  return {.box = base::Base64Encode(combined)};
}

std::optional<ProxyPasswords> OpenProxyPasswords(
    const SealedProxyPasswords& sealed,
    base::span<const uint8_t> secret) {
  const std::optional<std::vector<uint8_t>> combined =
      base::Base64Decode(sealed.box);
  if (!combined || combined->size() < kNonceSize) {
    return std::nullopt;
  }
  const auto [nonce, ciphertext] = base::span(*combined).split_at(kNonceSize);
  const std::optional<std::vector<uint8_t>> json = crypto::aead::Open(
      crypto::aead::AES_256_GCM, SealingKey(secret), ciphertext, nonce, {});
  if (!json) {
    return std::nullopt;
  }
  return ProxyPasswordsFromJson(base::as_string_view(*json));
}

std::optional<ProxyPasswords> ProxyPasswordsFromJson(std::string_view json) {
  // A dictionary of strings, as JSONDecoder takes it: anything else fails.
  const std::optional<base::Value> value =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    return std::nullopt;
  }
  ProxyPasswords passwords;
  for (const auto [id, password] : value->GetDict()) {
    if (!password.is_string()) {
      return std::nullopt;
    }
    passwords[id] = password.GetString();
  }
  return passwords;
}

ProxyPasswordVault::Sealing::Sealing() = default;

ProxyPasswordVault::Sealing::Sealing(
    base::RepeatingCallback<
        base::expected<std::optional<SealedProxyPasswords>, FileError>()> load,
    base::RepeatingClosure remove)
    : load(std::move(load)), remove(std::move(remove)) {}

ProxyPasswordVault::Sealing::Sealing(const Sealing&) = default;
ProxyPasswordVault::Sealing& ProxyPasswordVault::Sealing::operator=(
    const Sealing&) = default;
ProxyPasswordVault::Sealing::~Sealing() = default;

ProxyPasswordVault::Item::Item() = default;

ProxyPasswordVault::Item::Item(
    base::RepeatingCallback<std::optional<ProxyPasswords>()> read,
    base::RepeatingClosure remove)
    : read(std::move(read)), remove(std::move(remove)) {}

ProxyPasswordVault::Item::Item(const Item&) = default;
ProxyPasswordVault::Item& ProxyPasswordVault::Item::operator=(const Item&) =
    default;
ProxyPasswordVault::Item::~Item() = default;

ProxyPasswordVault::ProxyPasswordVault(base::RepeatingCallback<Secret()> secret,
                                       Sealing sealing,
                                       Item item)
    : secret_(std::move(secret)),
      sealing_(std::move(sealing)),
      item_(std::move(item)) {}

ProxyPasswordVault::ProxyPasswordVault(const ProxyPasswordVault&) = default;
ProxyPasswordVault& ProxyPasswordVault::operator=(const ProxyPasswordVault&) =
    default;
ProxyPasswordVault::~ProxyPasswordVault() = default;

std::optional<ProxyPasswords> ProxyPasswordVault::Read() const {
  const base::expected<std::optional<SealedProxyPasswords>, FileError> sealed =
      sealing_.load.Run();
  if (!sealed.has_value()) {
    return std::nullopt;
  }

  if (*sealed) {
    const Secret secret = secret_.Run();
    if (secret.kind != Secret::Kind::kKey) {
      return std::nullopt;
    }
    return OpenProxyPasswords(**sealed, secret.key);
  }

  return item_.read.Run();
}

void ProxyPasswordVault::Delete() const {
  sealing_.remove.Run();
  item_.remove.Run();
}

}  // namespace pane::model
