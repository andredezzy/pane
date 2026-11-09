// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_PROXY_PASSWORD_VAULT_H_
#define PANE_MODEL_MAC_PROXY_PASSWORD_VAULT_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "pane/model/persisted_file.h"

namespace pane::model {

// Proxy passwords by profile id.
using ProxyPasswords = std::map<std::string, std::string>;

// Proxy passwords as an earlier Pane sealed them under the engine's key (ADR
// 0069): AES-GCM, with a key HKDF-SHA256 derives from the "Pane Safe Storage"
// secret. Read once since ADR 0113, which seals them in profiles.json.
struct SealedProxyPasswords {
  // nullopt for a value that isn't one.
  static std::optional<SealedProxyPasswords> FromValue(
      const base::Value& value);
  base::Value ToValue() const;

  friend bool operator==(const SealedProxyPasswords&,
                         const SealedProxyPasswords&) = default;

  // AES-GCM's combined box (nonce, ciphertext, and tag), in base64.
  std::string box;
};

SealedProxyPasswords SealProxyPasswords(const ProxyPasswords& passwords,
                                        base::span<const uint8_t> secret);

// nullopt for another secret's box, or a box changed since it was sealed.
std::optional<ProxyPasswords> OpenProxyPasswords(
    const SealedProxyPasswords& sealed,
    base::span<const uint8_t> secret);

// The passwords as JSON keeps them, a dictionary of strings, as JSONDecoder
// read it: nullopt for anything else.
std::optional<ProxyPasswords> ProxyPasswordsFromJson(std::string_view json);

// Where an earlier Pane kept the proxy passwords, apart from profiles.json:
// sealed under the engine's key (ADR 0069), or, before that, in a Keychain
// item of their own ("Pane Proxy Passwords"). Since ADR 0113 they are in
// profiles.json, sealed with it: these are read once, when an earlier Pane's
// plain profiles.json is, then deleted. The app gives it the Keychain and the
// file.
class ProxyPasswordVault {
 public:
  // The engine's key, as the Keychain gives it.
  struct Secret {
    enum class Kind {
      kKey,
      // The engine has made none yet: a fresh install, before its first
      // launch is far enough along.
      kMissing,
      // The user denied it, or the Keychain failed.
      kUnavailable,
    };

    Kind kind = Kind::kUnavailable;
    // The item's stored bytes, for kKey.
    std::vector<uint8_t> key;
  };

  // The file they were sealed in: nullopt from `load` when there is none,
  // and an error when it would not read.
  struct Sealing {
    Sealing();
    Sealing(
        base::RepeatingCallback<
            base::expected<std::optional<SealedProxyPasswords>, FileError>()>
            load,
        base::RepeatingClosure remove);
    Sealing(const Sealing&);
    Sealing& operator=(const Sealing&);
    ~Sealing();

    base::RepeatingCallback<
        base::expected<std::optional<SealedProxyPasswords>, FileError>()>
        load;
    base::RepeatingClosure remove;
  };

  // The item of their own: nullopt from `read` when it could not be read.
  struct Item {
    Item();
    Item(base::RepeatingCallback<std::optional<ProxyPasswords>()> read,
         base::RepeatingClosure remove);
    Item(const Item&);
    Item& operator=(const Item&);
    ~Item();

    base::RepeatingCallback<std::optional<ProxyPasswords>()> read;
    base::RepeatingClosure remove;
  };

  ProxyPasswordVault(base::RepeatingCallback<Secret()> secret,
                     Sealing sealing,
                     Item item);
  ProxyPasswordVault(const ProxyPasswordVault&);
  ProxyPasswordVault& operator=(const ProxyPasswordVault&);
  ~ProxyPasswordVault();

  // nullopt when the passwords could not be read (the user denied the key,
  // say).
  std::optional<ProxyPasswords> Read() const;

  // Once the profiles are sealed with them, and for the panic wipe.
  void Delete() const;

 private:
  base::RepeatingCallback<Secret()> secret_;
  Sealing sealing_;
  Item item_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_PROXY_PASSWORD_VAULT_H_
