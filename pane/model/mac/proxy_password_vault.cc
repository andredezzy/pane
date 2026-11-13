// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/proxy_password_vault.h"

#include <string_view>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "crypto/hash.h"
#include "crypto/kdf.h"
#include "pane/model/data_key.h"

namespace pane::model {

namespace {

Key SealingKey(base::span<const uint8_t> secret) {
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
  return {.box = SealBox(SealingKey(secret), base::as_byte_span(json))};
}

std::optional<ProxyPasswords> OpenProxyPasswords(
    const SealedProxyPasswords& sealed,
    base::span<const uint8_t> secret) {
  const base::expected<std::vector<uint8_t>, BoxError> json =
      OpenBox(SealingKey(secret), sealed.box);
  if (!json.has_value()) {
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
