// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/proxy_password_vault.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/synchronization/lock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's ProxyPasswordSealTests and ProxyPasswordVaultTests.

std::vector<uint8_t> Bytes(std::string_view text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

// Seals the passwords, which open with the same secret alone.
TEST(ProxyPasswordSealTest, RoundTrip) {
  const std::vector<uint8_t> secret = Bytes("engine-safe-storage-secret");
  const ProxyPasswords passwords = {{"a", "s3cret"}, {"b", "hunter2"}};
  const SealedProxyPasswords sealed = SealProxyPasswords(passwords, secret);

  EXPECT_EQ(sealed.box.find("s3cret"), std::string::npos);
  EXPECT_EQ(OpenProxyPasswords(sealed, secret), passwords);
  EXPECT_EQ(OpenProxyPasswords(sealed, Bytes("another")), std::nullopt);
}

// A box changed since it was sealed does not open.
TEST(ProxyPasswordSealTest, Tampered) {
  const std::vector<uint8_t> secret = Bytes("engine-safe-storage-secret");
  const SealedProxyPasswords sealed =
      SealProxyPasswords({{"a", "s3cret"}}, secret);
  std::optional<std::vector<uint8_t>> bytes = base::Base64Decode(sealed.box);
  ASSERT_TRUE(bytes);
  ASSERT_FALSE(bytes->empty());
  bytes->back() ^= 0x01;

  EXPECT_EQ(OpenProxyPasswords({.box = base::Base64Encode(*bytes)}, secret),
            std::nullopt);
}

// The Keychain and the data directory, in memory.
class Places {
 public:
  using Secret = ProxyPasswordVault::Secret;

  Places() { secret_.key = Bytes("engine-key"); }

  void set_secret(Secret secret) {
    base::AutoLock lock(lock_);
    secret_ = std::move(secret);
  }
  void set_sealed(std::optional<SealedProxyPasswords> sealed) {
    base::AutoLock lock(lock_);
    sealed_ = std::move(sealed);
  }
  void set_item(std::optional<ProxyPasswords> item) {
    base::AutoLock lock(lock_);
    item_ = std::move(item);
  }
  std::optional<SealedProxyPasswords> sealed() {
    base::AutoLock lock(lock_);
    return sealed_;
  }
  std::optional<ProxyPasswords> item() {
    base::AutoLock lock(lock_);
    return item_;
  }

  ProxyPasswordVault Vault() {
    return ProxyPasswordVault(
        base::BindRepeating(&Places::GetSecret, base::Unretained(this)),
        ProxyPasswordVault::Sealing(
            base::BindRepeating(&Places::LoadSealed, base::Unretained(this)),
            base::BindRepeating(&Places::RemoveSealed, base::Unretained(this))),
        ProxyPasswordVault::Item(
            base::BindRepeating(&Places::ReadItem, base::Unretained(this)),
            base::BindRepeating(&Places::RemoveItem, base::Unretained(this))));
  }

 private:
  Secret GetSecret() {
    base::AutoLock lock(lock_);
    return secret_;
  }
  base::expected<std::optional<SealedProxyPasswords>, FileError> LoadSealed() {
    return sealed();
  }
  void RemoveSealed() { set_sealed(std::nullopt); }
  // What the item holds, none as empty.
  std::optional<ProxyPasswords> ReadItem() {
    return item().value_or(ProxyPasswords());
  }
  void RemoveItem() { set_item(std::nullopt); }

  base::Lock lock_;
  Secret secret_{.kind = Secret::Kind::kKey};
  std::optional<SealedProxyPasswords> sealed_;
  std::optional<ProxyPasswords> item_;
};

// An earlier Pane's box opens with the engine's key.
TEST(ProxyPasswordVaultTest, Sealed) {
  Places places;
  places.set_sealed(SealProxyPasswords({{"a", "s3cret"}}, Bytes("engine-key")));

  EXPECT_EQ(places.Vault().Read(), (ProxyPasswords{{"a", "s3cret"}}));
}

// The item an earlier Pane kept them in reads too.
TEST(ProxyPasswordVaultTest, Item) {
  Places places;
  places.set_item(ProxyPasswords{{"a", "from-item"}});

  EXPECT_EQ(places.Vault().Read(), (ProxyPasswords{{"a", "from-item"}}));
}

// A key the user denies, or another key, reads nothing.
TEST(ProxyPasswordVaultTest, Unreadable) {
  Places places;
  places.set_sealed(SealProxyPasswords({{"a", "s3cret"}}, Bytes("engine-key")));

  places.set_secret({.kind = Places::Secret::Kind::kUnavailable});
  EXPECT_EQ(places.Vault().Read(), std::nullopt);

  places.set_secret(
      {.kind = Places::Secret::Kind::kKey, .key = Bytes("another")});
  EXPECT_EQ(places.Vault().Read(), std::nullopt);
  EXPECT_TRUE(places.sealed());
}

// Once the profiles are sealed with them, neither the box nor the item stays.
TEST(ProxyPasswordVaultTest, Deletes) {
  Places places;
  places.set_sealed(SealProxyPasswords({{"a", "s3cret"}}, Bytes("engine-key")));
  places.set_item(ProxyPasswords{{"b", "stale"}});

  places.Vault().Delete();

  EXPECT_EQ(places.sealed(), std::nullopt);
  EXPECT_EQ(places.item(), std::nullopt);
}

}  // namespace
}  // namespace pane::model
