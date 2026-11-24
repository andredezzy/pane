// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_TEST_KEYCHAIN_H_
#define PANE_MODEL_MAC_TEST_KEYCHAIN_H_

#include <MacTypes.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pane/model/mac/safe_storage.h"

namespace base {
class WaitableEvent;
}  // namespace base

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

  // A call the Keychain was asked, whole: its name, and each key of its query
  // with its value as text, both by Security's own names (kSecClass is
  // "class"): a string as it is, a boolean "true" or "false", data as its
  // bytes, and an LAContext as whether it never asks the user. The generic
  // password's own calls, which take no query, name their service, account,
  // and password the same way.
  using Call = std::pair<std::string, std::map<std::string, std::string>>;

  // Every call it was asked, in order.
  std::vector<Call> calls() const;

  // Whether Keychain Services could have shown a prompt as the last delete
  // ran (SecKeychainGetUserInteractionAllowed); none before a delete.
  std::optional<bool> prompt_allowed_at_delete() const;

  // Every read from now waits for `gate`, as a Keychain that asks the user
  // first does; null answers at once again. The gate outlives the reads.
  void HoldReadsUntil(base::WaitableEvent* gate);

 private:
  class Fake;

  std::unique_ptr<Fake> fake_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_TEST_KEYCHAIN_H_
