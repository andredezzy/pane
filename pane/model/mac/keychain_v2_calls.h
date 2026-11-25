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

#include <string_view>
#include <vector>

#include "base/apple/scoped_cftyperef.h"
#include "base/types/expected.h"

namespace crypto::apple {
class KeychainV2;
}

namespace pane::model {

// The login Keychain, KeychainV2::GetInstance().
crypto::apple::KeychainV2& LoginKeychain();

// The Keychain under --use-mock-keychain (SafeStorage::Mock).
crypto::apple::KeychainV2& MockKeychainV2();

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

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_KEYCHAIN_V2_CALLS_H_
