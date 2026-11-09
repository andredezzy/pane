// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/proxy_password_store.h"

#include <Security/Security.h>

#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "pane/model/mac/bundle_identifier.h"
#include "pane/model/mac/file_operations.h"
#include "pane/model/persisted_file.h"

namespace pane::model {

namespace {

constexpr char kItemService[] = "Pane Proxy Passwords";
constexpr char kSealedName[] = "proxy-passwords";
constexpr char kMockName[] = "mock-keychain";

base::expected<std::optional<SealedProxyPasswords>, FileError> LoadSealed(
    const base::FilePath& data_directory) {
  base::expected<std::optional<base::Value>, FileError> state =
      PersistedFile(data_directory, kSealedName).Load();
  if (!state.has_value()) {
    return base::unexpected(state.error());
  }
  if (!*state) {
    return std::nullopt;
  }
  std::optional<SealedProxyPasswords> sealed =
      SealedProxyPasswords::FromValue(**state);
  if (!sealed) {
    return base::unexpected(FileError::kMalformed);
  }
  return sealed;
}

void RemoveSealed(const base::FilePath& data_directory) {
  base::DeleteFile(PersistedFile(data_directory, kSealedName).path());
}

}  // namespace

ProxyPasswordStore::ProxyPasswordStore(const base::FilePath& data_directory,
                                       SafeStorage safe_storage)
    : data_directory_(data_directory), safe_storage_(safe_storage) {
  accounts_.push_back(StandardizedPath(data_directory).value());
  if (data_directory.BaseName().value() == kBundleIdentifier) {
    accounts_.push_back(StandardizedPath(data_directory.DirName().Append(
                                             kFormerBundleIdentifier))
                            .value());
  }
}

ProxyPasswordStore::ProxyPasswordStore(const ProxyPasswordStore&) = default;
ProxyPasswordStore& ProxyPasswordStore::operator=(const ProxyPasswordStore&) =
    default;
ProxyPasswordStore::~ProxyPasswordStore() = default;

std::optional<ProxyPasswords> ProxyPasswordStore::Read() const {
  if (safe_storage_.is_mock()) {
    const base::expected<std::optional<base::Value>, FileError> state =
        PersistedFile(data_directory_, kMockName).Load();
    if (!state.has_value()) {
      return std::nullopt;
    }
    if (!*state) {
      return ProxyPasswords();
    }
    const base::DictValue* dict = (*state)->GetIfDict();
    if (!dict) {
      return std::nullopt;
    }
    ProxyPasswords passwords;
    for (const auto [id, password] : *dict) {
      if (!password.is_string()) {
        return std::nullopt;
      }
      passwords[id] = password.GetString();
    }
    return passwords;
  }

  std::optional<ProxyPasswords> passwords = Vault().Read();
  if (!passwords) {
    LOG(ERROR) << "Could not read the proxy passwords";
  }
  return passwords;
}

void ProxyPasswordStore::Delete() const {
  if (safe_storage_.is_mock()) {
    base::DeleteFile(PersistedFile(data_directory_, kMockName).path());
    return;
  }
  Vault().Delete();
}

ProxyPasswordVault ProxyPasswordStore::Vault() const {
  return ProxyPasswordVault(
      base::BindRepeating(&SafeStorage::EngineSecret,
                          base::Unretained(&safe_storage_)),
      ProxyPasswordVault::Sealing(
          base::BindRepeating(&LoadSealed, data_directory_),
          base::BindRepeating(&RemoveSealed, data_directory_)),
      ProxyPasswordVault::Item(
          base::BindRepeating(&ProxyPasswordStore::ReadItem,
                              base::Unretained(this)),
          base::BindRepeating(&ProxyPasswordStore::DeleteItem,
                              base::Unretained(this))));
}

// The item an earlier Pane kept them in, under the first account that has
// one.
std::optional<ProxyPasswords> ProxyPasswordStore::ReadItem() const {
  for (const std::string& account : accounts_) {
    const base::expected<std::vector<uint8_t>, OSStatus> found =
        safe_storage_.Find(kItemService, account);
    if (found.has_value()) {
      return ProxyPasswordsFromJson(base::as_string_view(*found));
    }
    if (found.error() != errSecItemNotFound) {
      LOG(ERROR) << "Could not read the proxy passwords' item: "
                 << found.error();
      return std::nullopt;
    }
  }
  return ProxyPasswords();
}

void ProxyPasswordStore::DeleteItem() const {
  for (const std::string& account : accounts_) {
    safe_storage_.DeleteKey(kItemService, account);
  }
}

}  // namespace pane::model
