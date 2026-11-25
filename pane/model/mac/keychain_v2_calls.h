// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_KEYCHAIN_V2_CALLS_H_
#define PANE_MODEL_MAC_KEYCHAIN_V2_CALLS_H_

// Chromium's Keychain seam, KeychainV2, reached from C++. Its header is
// Objective-C: it imports Foundation, CryptoTokenKit, and
// LocalAuthentication, and its methods take an NSArray and an LAContext, so
// C++ can neither call it nor subclass it. Each call here is a line of
// Objective-C++ (keychain_v2_calls.mm), and every one still goes through
// Chromium's KeychainV2 (ADR 0124).

#include <CoreFoundation/CoreFoundation.h>
#include <MacTypes.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "base/apple/scoped_cftyperef.h"
#include "base/containers/span.h"
#include "base/types/expected.h"

namespace crypto::apple {
class KeychainV2;
}

namespace pane::model {

// The login Keychain, KeychainV2::GetInstance().
crypto::apple::KeychainV2& LoginKeychain();

// KeychainV2's own calls on `keychain`.
OSStatus ItemAdd(crypto::apple::KeychainV2& keychain,
                 CFDictionaryRef attributes,
                 CFTypeRef* result);
OSStatus ItemCopyMatching(crypto::apple::KeychainV2& keychain,
                          CFDictionaryRef query,
                          CFTypeRef* result);
OSStatus ItemDelete(crypto::apple::KeychainV2& keychain, CFDictionaryRef query);
base::expected<std::vector<uint8_t>, OSStatus> FindGenericPassword(
    crypto::apple::KeychainV2& keychain,
    std::string_view service,
    std::string_view account);

// An LAContext that never asks the user (interactionNotAllowed), for a
// query's kSecUseAuthenticationContext: where a data protection keychain's
// item would ask, the call fails instead.
base::apple::ScopedCFTypeRef<CFTypeRef> ContextThatNeverAsks();

// Whether `value`, a query's kSecUseAuthenticationContext, never asks the
// user, as a Keychain in C++ reads it; nullopt for what isn't an LAContext.
std::optional<bool> NeverAsks(CFTypeRef value);

// A Keychain written in C++: the six item calls of KeychainV2's, each with
// its signature, which keychain() answers with. KeychainV2's other calls,
// for tokens and keys, stay its own, as for any KeychainV2. The mock
// Keychain and the tests' Keychain are written so.
class KeychainItems {
 public:
  KeychainItems();
  KeychainItems(const KeychainItems&) = delete;
  KeychainItems& operator=(const KeychainItems&) = delete;
  virtual ~KeychainItems();

  // A KeychainV2 that answers its item calls with this one's, for
  // SafeStorage and Chromium's own Keychain code. It lives as long as this.
  crypto::apple::KeychainV2& keychain();

  virtual OSStatus ItemAdd(CFDictionaryRef attributes, CFTypeRef* result) = 0;
  virtual OSStatus ItemCopyMatching(CFDictionaryRef query,
                                    CFTypeRef* result) = 0;
  virtual OSStatus ItemDelete(CFDictionaryRef query) = 0;
  virtual OSStatus ItemUpdate(CFDictionaryRef query,
                              CFDictionaryRef keychain_data) = 0;
  virtual OSStatus AddGenericPassword(std::string_view service_name,
                                      std::string_view account_name,
                                      base::span<const uint8_t> password) = 0;
  virtual base::expected<std::vector<uint8_t>, OSStatus> FindGenericPassword(
      std::string_view service_name,
      std::string_view account_name) = 0;

 private:
  class AsKeychainV2;

  std::unique_ptr<AsKeychainV2> keychain_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_KEYCHAIN_V2_CALLS_H_
