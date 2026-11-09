// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_SAFE_STORAGE_H_
#define PANE_MODEL_MAC_SAFE_STORAGE_H_

#include <MacTypes.h>
#include <stdint.h>

#include <optional>
#include <string_view>
#include <vector>

#include "base/memory/raw_ref.h"
#include "base/types/expected.h"
#include "pane/model/mac/proxy_password_vault.h"

namespace crypto::apple {
class KeychainV2;
}

namespace pane::model {

// The engine's key in the login Keychain (ADR 0037, ADR 0113): the generic
// password Chromium names "<app> Safe Storage", with the app's name as its
// account, by BRANDING ("Pane Safe Storage" and "Pane"), and makes as 16
// random bytes in base64. The data key is sealed under the item's stored
// bytes, that text as it is, not decoded (SealDataKey). Every call goes through
// Chromium's own Keychain code (KeychainV2, KeychainPassword) and may wait on
// the user, so it belongs where blocking is allowed. Only the Electron app's
// item, which a migration hands over (AdoptKey) and the panic wipe forgets
// (DeleteKey), is Pane's alone.
class SafeStorage {
 public:
  // The Keychain this process reads: the login Keychain, or, under
  // --use-mock-keychain, Mock(), as the engine's own reads it
  // (keychain_key_provider.mm).
  static SafeStorage ForProcess();

  // A scenario's launch, on the mock Keychain, where Pane's test hooks count
  // (ADR 0115).
  static bool UsesMockKeychain();

  // The mock Keychain: the engine's item holds the fixed secret every
  // scenario's data is sealed under (tests/lib/engine-key.ts), nothing else
  // is there, and nothing it is asked reaches the login Keychain.
  static SafeStorage Mock();

  explicit SafeStorage(crypto::apple::KeychainV2& keychain);
  SafeStorage(const SafeStorage&);
  SafeStorage& operator=(const SafeStorage&);
  ~SafeStorage();

  // Whether it is Mock(): the proxy passwords are then a file of the data
  // directory's (ProxyPasswordStore), as the Swift's test hooks kept them.
  bool is_mock() const { return mock_; }

  // The secret the data key is sealed under. When the engine has none yet and
  // `makes_one`, it is made here, as Chromium makes it, since a fresh install
  // needs the data key before the engine first reads its item; never once a
  // key is sealed, which a new secret would not open. nullopt when the user
  // denied it, or the Keychain failed.
  std::optional<std::vector<uint8_t>> DataSecret(bool makes_one) const;

  // The engine's key, which an earlier Pane sealed the proxy passwords under
  // (ADR 0069).
  ProxyPasswordVault::Secret EngineSecret() const;

  // Whether the engine has a key yet. Its attributes, not its secret, so it
  // asks the user nothing.
  bool HasEngineKey() const;

  // The Electron app's key, given to the engine's item, so the cookies a
  // migration copied still decrypt. macOS asks the user first.
  OSStatus AdoptKey(std::string_view service, std::string_view account) const;

  // Removes an item without asking the user anything: where macOS would ask,
  // it fails instead (errSecInteractionNotAllowed).
  OSStatus DeleteKey(std::string_view service, std::string_view account) const;

  // A generic password's stored bytes.
  base::expected<std::vector<uint8_t>, OSStatus> Find(
      std::string_view service,
      std::string_view account) const;

 private:
  SafeStorage(crypto::apple::KeychainV2& keychain, bool mock);

  raw_ref<crypto::apple::KeychainV2> keychain_;
  bool mock_ = false;
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_SAFE_STORAGE_H_
