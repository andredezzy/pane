// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/keychain_v2_calls.h"

#import <LocalAuthentication/LocalAuthentication.h>

#include "base/apple/foundation_util.h"
#include "base/memory/raw_ref.h"
#include "crypto/apple/keychain_v2.h"

namespace pane::model {

crypto::apple::KeychainV2& LoginKeychain() {
  return crypto::apple::KeychainV2::GetInstance();
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

std::optional<bool> NeverAsks(CFTypeRef value) {
  LAContext* context = base::apple::ObjCCast<LAContext>((__bridge id)value);
  if (!context) {
    return std::nullopt;
  }
  return context.interactionNotAllowed;
}

class KeychainItems::AsKeychainV2 : public crypto::apple::KeychainV2 {
 public:
  explicit AsKeychainV2(KeychainItems& items) : items_(items) {}
  ~AsKeychainV2() override = default;

  // KeychainV2:
  OSStatus ItemAdd(CFDictionaryRef attributes, CFTypeRef* result) override {
    return items_->ItemAdd(attributes, result);
  }
  OSStatus ItemCopyMatching(CFDictionaryRef query, CFTypeRef* result) override {
    return items_->ItemCopyMatching(query, result);
  }
  OSStatus ItemDelete(CFDictionaryRef query) override {
    return items_->ItemDelete(query);
  }
  OSStatus ItemUpdate(CFDictionaryRef query,
                      CFDictionaryRef keychain_data) override {
    return items_->ItemUpdate(query, keychain_data);
  }
  OSStatus AddGenericPassword(std::string_view service_name,
                              std::string_view account_name,
                              base::span<const uint8_t> password) override {
    return items_->AddGenericPassword(service_name, account_name, password);
  }
  base::expected<std::vector<uint8_t>, OSStatus> FindGenericPassword(
      std::string_view service_name,
      std::string_view account_name) override {
    return items_->FindGenericPassword(service_name, account_name);
  }

 private:
  const raw_ref<KeychainItems> items_;
};

KeychainItems::KeychainItems()
    : keychain_(std::make_unique<AsKeychainV2>(*this)) {}

KeychainItems::~KeychainItems() = default;

crypto::apple::KeychainV2& KeychainItems::keychain() {
  return *keychain_;
}

}  // namespace pane::model
