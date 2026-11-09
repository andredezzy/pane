// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/safe_storage.h"

#include <Security/Security.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "pane/model/mac/proxy_password_vault.h"
#include "pane/model/mac/test_keychain.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// The engine's Keychain item through Chromium's own code (the Swift app's
// SafeStorage), on a Keychain in memory: none of these reach the login
// Keychain.

std::vector<uint8_t> Bytes(std::string_view text) {
  return base::ToVector(base::as_byte_span(text));
}

// Found where none may be made: at launch, and once a key is sealed, which a
// new secret would not open.
TEST(SafeStorageTest, FindsWithoutMaking) {
  TestKeychain keychain;
  const SafeStorage storage = keychain.safe_storage();
  EXPECT_FALSE(storage.DataSecret(/*makes_one=*/false));
  EXPECT_FALSE(keychain.EngineSecret());

  keychain.SetEngineSecret("c2VjcmV0c2VjcmV0c2VjcmV0");
  EXPECT_EQ(storage.DataSecret(/*makes_one=*/false),
            Bytes("c2VjcmV0c2VjcmV0c2VjcmV0"));
}

// A fresh install's first open makes the engine's item as Chromium makes it:
// 16 random bytes as base64 text, the text itself the secret.
TEST(SafeStorageTest, MakesTheItemAsChromiumDoes) {
  TestKeychain keychain;
  const SafeStorage storage = keychain.safe_storage();
  const std::optional<std::vector<uint8_t>> secret =
      storage.DataSecret(/*makes_one=*/true);
  ASSERT_TRUE(secret);
  const std::string text(secret->begin(), secret->end());
  EXPECT_EQ(keychain.EngineSecret(), text);
  std::string decoded;
  ASSERT_TRUE(base::Base64Decode(text, &decoded));
  EXPECT_EQ(decoded.size(), 16u);

  // Made once: the next open finds it.
  EXPECT_EQ(storage.DataSecret(/*makes_one=*/true), secret);
  EXPECT_EQ(storage.DataSecret(/*makes_one=*/false), secret);
}

// Denied, or a locked session: no secret, and nothing made in its place.
TEST(SafeStorageTest, DeniedGivesNothing) {
  TestKeychain keychain;
  keychain.FailWith(errSecAuthFailed);
  const SafeStorage storage = keychain.safe_storage();
  EXPECT_FALSE(storage.DataSecret(/*makes_one=*/true));
  EXPECT_FALSE(storage.DataSecret(/*makes_one=*/false));
  EXPECT_EQ(storage.EngineSecret().kind,
            ProxyPasswordVault::Secret::Kind::kUnavailable);
  keychain.FailWith(errSecSuccess);
  EXPECT_FALSE(keychain.EngineSecret());
}

// The engine's key for the proxy passwords an earlier Pane sealed (ADR 0069).
TEST(SafeStorageTest, EngineSecret) {
  TestKeychain keychain;
  const SafeStorage storage = keychain.safe_storage();
  EXPECT_EQ(storage.EngineSecret().kind,
            ProxyPasswordVault::Secret::Kind::kMissing);
  EXPECT_FALSE(storage.HasEngineKey());

  keychain.SetEngineSecret("key");
  const ProxyPasswordVault::Secret secret = storage.EngineSecret();
  EXPECT_EQ(secret.kind, ProxyPasswordVault::Secret::Kind::kKey);
  EXPECT_EQ(secret.key, Bytes("key"));
  EXPECT_TRUE(storage.HasEngineKey());
}

// A migration hands the Electron app's key to the engine's item, in place of
// one the engine made, so the cookies it copied still decrypt.
TEST(SafeStorageTest, AdoptsTheElectronAppsKey) {
  TestKeychain keychain;
  const SafeStorage storage = keychain.safe_storage();
  EXPECT_EQ(storage.AdoptKey("@pane/veil Safe Storage", "@pane/veil"),
            errSecItemNotFound);
  EXPECT_FALSE(keychain.EngineSecret());

  keychain.SetItem("@pane/veil Safe Storage", "@pane/veil", "electron");
  keychain.SetEngineSecret("made");
  EXPECT_EQ(storage.AdoptKey("@pane/veil Safe Storage", "@pane/veil"),
            errSecSuccess);
  EXPECT_EQ(keychain.EngineSecret(), "electron");
  EXPECT_EQ(keychain.Item("@pane/veil Safe Storage", "@pane/veil"), "electron");
}

TEST(SafeStorageTest, DeletesAnItem) {
  TestKeychain keychain;
  const SafeStorage storage = keychain.safe_storage();
  keychain.SetItem("Pane Proxy Passwords", "/data", "{}");
  EXPECT_EQ(storage.DeleteKey("Pane Proxy Passwords", "/data"), errSecSuccess);
  EXPECT_FALSE(keychain.Item("Pane Proxy Passwords", "/data"));
  EXPECT_EQ(storage.DeleteKey("Pane Proxy Passwords", "/data"),
            errSecItemNotFound);
}

// The mock Keychain gives every scenario the secret its data is sealed under
// (tests/lib/engine-key.ts), makes nothing, and holds nothing else.
TEST(SafeStorageTest, TheMockKeychain) {
  const SafeStorage mock = SafeStorage::Mock();
  EXPECT_TRUE(mock.is_mock());
  EXPECT_EQ(mock.DataSecret(/*makes_one=*/false), Bytes("Pane mock keychain"));
  EXPECT_EQ(mock.DataSecret(/*makes_one=*/true), Bytes("Pane mock keychain"));
  EXPECT_EQ(mock.EngineSecret().key, Bytes("Pane mock keychain"));
  EXPECT_TRUE(mock.HasEngineKey());
  EXPECT_EQ(mock.AdoptKey("@pane/veil Safe Storage", "@pane/veil"),
            errSecItemNotFound);
  EXPECT_FALSE(mock.Find("Pane Proxy Passwords", "/data").has_value());

  TestKeychain keychain;
  EXPECT_FALSE(keychain.safe_storage().is_mock());
}

}  // namespace
}  // namespace pane::model
