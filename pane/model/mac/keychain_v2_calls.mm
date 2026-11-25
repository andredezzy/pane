// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/keychain_v2_calls.h"

#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>

#include <string>

#include "base/apple/foundation_util.h"
#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/no_destructor.h"
#include "base/strings/sys_string_conversions.h"
#include "components/os_crypt/common/keychain_password_mac.h"
#include "crypto/apple/keychain_v2.h"
#include "pane/model/test_hooks.h"

namespace pane::model {

namespace {

bool IsEngineItem(std::string_view service, std::string_view account) {
  return service == KeychainPassword::GetServiceName() &&
         account == KeychainPassword::GetAccountName();
}

// A scenario's Keychain that won't give the engine's item, as one the user
// denied (PANE_MOCK_KEYCHAIN_DENIES): the PIN screen's 07.7.
bool MockDenies() {
  return TestHook("PANE_MOCK_KEYCHAIN_DENIES").has_value();
}

// The Keychain under --use-mock-keychain: the engine's item holds the fixed
// secret, and nothing else is there. Nothing it is asked reaches the login
// Keychain, and nothing is kept.
class MockKeychain : public crypto::apple::KeychainV2 {
 public:
  MockKeychain() = default;
  MockKeychain(const MockKeychain&) = delete;
  MockKeychain& operator=(const MockKeychain&) = delete;
  ~MockKeychain() override = default;

  OSStatus ItemAdd(CFDictionaryRef attributes, CFTypeRef* result) override {
    return errSecSuccess;
  }

  OSStatus ItemCopyMatching(CFDictionaryRef query, CFTypeRef* result) override {
    if (MockDenies()) {
      return errSecAuthFailed;
    }
    CFStringRef service = base::apple::GetValueFromDictionary<CFStringRef>(
        query, kSecAttrService);
    CFStringRef account = base::apple::GetValueFromDictionary<CFStringRef>(
        query, kSecAttrAccount);
    return service && account &&
                   IsEngineItem(base::SysCFStringRefToUTF8(service),
                                base::SysCFStringRefToUTF8(account))
               ? errSecSuccess
               : errSecItemNotFound;
  }

  OSStatus ItemDelete(CFDictionaryRef query) override {
    return errSecItemNotFound;
  }

  OSStatus ItemUpdate(CFDictionaryRef query,
                      CFDictionaryRef keychain_data) override {
    return errSecItemNotFound;
  }

  OSStatus AddGenericPassword(std::string_view service_name,
                              std::string_view account_name,
                              base::span<const uint8_t> password) override {
    return errSecSuccess;
  }

  base::expected<std::vector<uint8_t>, OSStatus> FindGenericPassword(
      std::string_view service_name,
      std::string_view account_name) override {
    if (!IsEngineItem(service_name, account_name)) {
      return base::unexpected(errSecItemNotFound);
    }
    if (MockDenies()) {
      return base::unexpected(errSecAuthFailed);
    }
    return base::ToVector(base::as_byte_span(kMockKeychainSecret));
  }
};

}  // namespace

crypto::apple::KeychainV2& LoginKeychain() {
  return crypto::apple::KeychainV2::GetInstance();
}

crypto::apple::KeychainV2& MockKeychainV2() {
  static base::NoDestructor<MockKeychain> mock;
  return *mock;
}

OSStatus ItemAdd(crypto::apple::KeychainV2& keychain,
                 CFDictionaryRef attributes,
                 CFTypeRef* result) {
  return keychain.ItemAdd(attributes, result);
}

OSStatus ItemCopyMatching(crypto::apple::KeychainV2& keychain,
                          CFDictionaryRef query,
                          CFTypeRef* result) {
  return keychain.ItemCopyMatching(query, result);
}

OSStatus ItemDelete(crypto::apple::KeychainV2& keychain,
                    CFDictionaryRef query) {
  return keychain.ItemDelete(query);
}

base::expected<std::vector<uint8_t>, OSStatus> FindGenericPassword(
    crypto::apple::KeychainV2& keychain,
    std::string_view service,
    std::string_view account) {
  return keychain.FindGenericPassword(service, account);
}

base::apple::ScopedCFTypeRef<CFTypeRef> ContextThatNeverAsks() {
  LAContext* context = [[LAContext alloc] init];
  context.interactionNotAllowed = YES;
  return base::apple::ScopedCFTypeRef<CFTypeRef>(
      (__bridge_retained CFTypeRef)context);
}

}  // namespace pane::model
