// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/proxy_password_store.h"

#include <Security/Security.h>

#include <optional>
#include <string_view>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "pane/model/mac/bundle_identifier.h"
#include "pane/model/mac/file_operations.h"
#include "pane/model/mac/proxy_password_vault.h"
#include "pane/model/mac/safe_storage.h"
#include "pane/model/mac/test_keychain.h"
#include "pane/model/persisted_file.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Where an earlier Pane kept the proxy passwords (the Swift app's
// ProxyPasswordStore), each place read once and deleted, on the Keychain in
// memory.

constexpr std::string_view kSecret = "c2VjcmV0c2VjcmV0c2VjcmV0";
constexpr char kItemService[] = "Pane Proxy Passwords";

class ProxyPasswordStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    data_ = directory_.GetPath().Append(kBundleIdentifier);
    ASSERT_TRUE(base::CreateDirectory(data_));
  }

  ProxyPasswordStore Store() {
    return ProxyPasswordStore(data_, keychain_.safe_storage());
  }

  std::string Account(const base::FilePath& directory) const {
    return StandardizedPath(directory).value();
  }

  base::ScopedTempDir directory_;
  base::FilePath data_;
  TestKeychain keychain_;
};

// proxy-passwords.json, sealed under the engine's key (ADR 0069): read with
// the item's stored bytes, then deleted with the item before it.
TEST_F(ProxyPasswordStoreTest, ReadsTheSealedFile) {
  keychain_.SetEngineSecret(kSecret);
  ASSERT_TRUE(PersistedFile(data_, "proxy-passwords")
                  .Save(SealProxyPasswords({{"alpha", "secret"}},
                                           base::as_byte_span(kSecret))
                            .ToValue()));
  keychain_.SetItem(kItemService, Account(data_), R"({"alpha":"older"})");

  EXPECT_EQ(Store().Read(), (ProxyPasswords{{"alpha", "secret"}}));

  Store().Delete();
  EXPECT_FALSE(base::PathExists(data_.Append("proxy-passwords.json")));
  EXPECT_FALSE(keychain_.Item(kItemService, Account(data_)));
  EXPECT_EQ(keychain_.EngineSecret(), kSecret);
}

// Sealed under another engine key, or with the key unreadable: no passwords,
// rather than wrong ones.
TEST_F(ProxyPasswordStoreTest, AnotherKeyReadsNothing) {
  keychain_.SetEngineSecret("b3RoZXJvdGhlcm90aGVyb3Ro");
  ASSERT_TRUE(PersistedFile(data_, "proxy-passwords")
                  .Save(SealProxyPasswords({{"alpha", "secret"}},
                                           base::as_byte_span(kSecret))
                            .ToValue()));
  EXPECT_FALSE(Store().Read());

  keychain_.FailWith(errSecAuthFailed);
  EXPECT_FALSE(Store().Read());
}

// Before that, a Keychain item of their own, under the data directory's path,
// or under the former identifier's for a data root ADR 0114 moved.
TEST_F(ProxyPasswordStoreTest, ReadsTheirOwnItem) {
  keychain_.SetItem(kItemService, Account(data_), R"({"alpha":"secret"})");
  EXPECT_EQ(Store().Read(), (ProxyPasswords{{"alpha", "secret"}}));
  Store().Delete();
  EXPECT_FALSE(keychain_.Item(kItemService, Account(data_)));

  const base::FilePath former =
      directory_.GetPath().Append(kFormerBundleIdentifier);
  keychain_.SetItem(kItemService, Account(former), R"({"beta":"other"})");
  EXPECT_EQ(Store().Read(), (ProxyPasswords{{"beta", "other"}}));
  Store().Delete();
  EXPECT_FALSE(keychain_.Item(kItemService, Account(former)));
}

// Nothing stored reads as none; an item that isn't a dictionary of strings,
// or one the Keychain won't give, reads as unreadable.
TEST_F(ProxyPasswordStoreTest, NoneAndUnreadable) {
  EXPECT_EQ(Store().Read(), ProxyPasswords());

  keychain_.SetItem(kItemService, Account(data_), R"({"alpha":1})");
  EXPECT_FALSE(Store().Read());

  keychain_.SetItem(kItemService, Account(data_), R"({"alpha":"secret"})");
  keychain_.FailWith(errSecInteractionNotAllowed);
  EXPECT_FALSE(Store().Read());
}

// On the mock Keychain they are a file of the data directory's, and the
// Keychain is never asked.
TEST_F(ProxyPasswordStoreTest, TheMockKeychainsFile) {
  ProxyPasswordStore store(data_, SafeStorage::Mock());
  EXPECT_EQ(store.Read(), ProxyPasswords());

  base::DictValue passwords;
  passwords.Set("alpha", "secret");
  ASSERT_TRUE(PersistedFile(data_, "mock-keychain")
                  .Save(base::Value(std::move(passwords))));
  EXPECT_EQ(store.Read(), (ProxyPasswords{{"alpha", "secret"}}));

  store.Delete();
  EXPECT_FALSE(base::PathExists(data_.Append("mock-keychain.json")));
}

}  // namespace
}  // namespace pane::model
