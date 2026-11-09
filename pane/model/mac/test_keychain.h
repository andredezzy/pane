// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_TEST_KEYCHAIN_H_
#define PANE_MODEL_MAC_TEST_KEYCHAIN_H_

#include <MacTypes.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "pane/model/mac/safe_storage.h"

namespace pane::model {

// A Keychain in memory for the tests, through KeychainV2, the seam Chromium's
// own Keychain code takes: nothing reaches the login Keychain. Thread-safe,
// since the vault and the lock read it off the test's sequence.
class TestKeychain {
 public:
  TestKeychain();
  TestKeychain(const TestKeychain&) = delete;
  TestKeychain& operator=(const TestKeychain&) = delete;
  ~TestKeychain();

  SafeStorage safe_storage();

  void SetItem(std::string_view service,
               std::string_view account,
               std::string_view bytes);
  std::optional<std::string> Item(std::string_view service,
                                  std::string_view account) const;

  // The engine's item ("Pane Safe Storage", "Pane").
  void SetEngineSecret(std::string_view bytes);
  std::optional<std::string> EngineSecret() const;

  // Every call answers `status` from now, as a denied prompt or a locked
  // session does; errSecSuccess answers again.
  void FailWith(OSStatus status);

  // How many times an item's secret was read.
  int reads() const;

 private:
  class Fake;

  std::unique_ptr<Fake> fake_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_TEST_KEYCHAIN_H_
