// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_PROXY_PASSWORD_STORE_H_
#define PANE_MODEL_MAC_PROXY_PASSWORD_STORE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "pane/model/mac/proxy_password_vault.h"
#include "pane/model/mac/safe_storage.h"

namespace pane::model {

// Where an earlier Pane kept the proxy passwords (ProxyPasswordVault):
// proxy-passwords.json, sealed under the engine's "Pane Safe Storage" key (ADR
// 0069), or, before that, a Keychain item of their own, "Pane Proxy
// Passwords", whose account is the data directory's path (under the former
// identifier's folder, for a data root ADR 0114 moved). Since ADR 0113 they
// are in profiles.json, sealed with it: these are read once, when an earlier
// Pane's plain profiles.json is, and deleted once the profiles are sealed with
// them, and by the panic wipe. On the mock Keychain (SafeStorage::Mock), as
// the engine's own item, they are a file in the data directory instead
// (mock-keychain.json), and the login Keychain is never touched: scenarios
// run so.
class ProxyPasswordStore {
 public:
  ProxyPasswordStore(const base::FilePath& data_directory,
                     SafeStorage safe_storage);
  ProxyPasswordStore(const ProxyPasswordStore&);
  ProxyPasswordStore& operator=(const ProxyPasswordStore&);
  ~ProxyPasswordStore();

  // nullopt when they could not be read (the user denied the key, say): the
  // profiles then have no password for those proxies.
  std::optional<ProxyPasswords> Read() const;

  // Once the profiles are sealed with them, and for the panic wipe. Without
  // asking: where macOS would ask, the item stays.
  void Delete() const;

 private:
  ProxyPasswordVault Vault() const;
  std::optional<ProxyPasswords> ReadItem() const;
  void DeleteItem() const;

  base::FilePath data_directory_;
  SafeStorage safe_storage_;
  // The data directory's path, and where it was before ADR 0114 moved it.
  std::vector<std::string> accounts_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_PROXY_PASSWORD_STORE_H_
