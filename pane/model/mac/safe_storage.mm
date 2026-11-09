// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/safe_storage.h"

#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>

#include <string>

#include "base/apple/bridging.h"
#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/sys_string_conversions.h"
#include "components/os_crypt/common/keychain_password_mac.h"
#include "components/os_crypt/common/os_crypt_switches.h"
#include "crypto/apple/keychain_v2.h"

namespace pane::model {

namespace {

// A scenario's secret: it opens none of the user's data, sealed under the
// Keychain's (ADR 0113).
constexpr std::string_view kMockSecret = "Pane mock keychain";

bool IsEngineItem(std::string_view service, std::string_view account) {
  return service == KeychainPassword::GetServiceName() &&
         account == KeychainPassword::GetAccountName();
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
    return base::ToVector(base::as_byte_span(kMockSecret));
  }
};

NSDictionary* GenericPassword(std::string_view service,
                              std::string_view account) {
  return @{
    base::apple::CFToNSPtrCast(kSecClass) :
        base::apple::CFToNSPtrCast(kSecClassGenericPassword),
    base::apple::CFToNSPtrCast(kSecAttrService) :
        base::SysUTF8ToNSString(service),
    base::apple::CFToNSPtrCast(kSecAttrAccount) :
        base::SysUTF8ToNSString(account),
  };
}

}  // namespace

// static
SafeStorage SafeStorage::ForProcess() {
  if (UsesMockKeychain()) {
    return Mock();
  }
  return SafeStorage(crypto::apple::KeychainV2::GetInstance());
}

// static
bool SafeStorage::UsesMockKeychain() {
  return base::CommandLine::InitializedForCurrentProcess() &&
         base::CommandLine::ForCurrentProcess()->HasSwitch(
             os_crypt::switches::kUseMockKeychain);
}

// static
SafeStorage SafeStorage::Mock() {
  static base::NoDestructor<MockKeychain> mock;
  return SafeStorage(*mock, /*mock=*/true);
}

SafeStorage::SafeStorage(crypto::apple::KeychainV2& keychain)
    : SafeStorage(keychain, /*mock=*/false) {}

SafeStorage::SafeStorage(crypto::apple::KeychainV2& keychain, bool mock)
    : keychain_(keychain), mock_(mock) {}

SafeStorage::SafeStorage(const SafeStorage&) = default;
SafeStorage& SafeStorage::operator=(const SafeStorage&) = default;
SafeStorage::~SafeStorage() = default;

std::optional<std::vector<uint8_t>> SafeStorage::DataSecret(
    bool makes_one) const {
  if (makes_one) {
    // Found, or made as Chromium makes it (keychain_password_mac.mm): 128
    // random bits in base64, stored as that text. Empty when the Keychain
    // would not give or take it.
    const std::string password = KeychainPassword(*keychain_).GetPassword();
    if (password.empty()) {
      return std::nullopt;
    }
    return base::ToVector(base::as_byte_span(password));
  }

  base::expected<std::vector<uint8_t>, OSStatus> found = Find(
      KeychainPassword::GetServiceName(), KeychainPassword::GetAccountName());
  if (!found.has_value()) {
    if (found.error() != errSecItemNotFound) {
      LOG(ERROR) << "Could not read the engine's key: " << found.error();
    }
    return std::nullopt;
  }
  return std::move(*found);
}

ProxyPasswordVault::Secret SafeStorage::EngineSecret() const {
  base::expected<std::vector<uint8_t>, OSStatus> found = Find(
      KeychainPassword::GetServiceName(), KeychainPassword::GetAccountName());
  if (found.has_value()) {
    return {.kind = ProxyPasswordVault::Secret::Kind::kKey,
            .key = std::move(*found)};
  }
  if (found.error() == errSecItemNotFound) {
    return {.kind = ProxyPasswordVault::Secret::Kind::kMissing};
  }
  LOG(ERROR) << "Could not read the engine's key: " << found.error();
  return {.kind = ProxyPasswordVault::Secret::Kind::kUnavailable};
}

bool SafeStorage::HasEngineKey() const {
  NSMutableDictionary* query =
      [GenericPassword(KeychainPassword::GetServiceName(),
                       KeychainPassword::GetAccountName()) mutableCopy];
  query[base::apple::CFToNSPtrCast(kSecReturnAttributes)] = @YES;
  return keychain_->ItemCopyMatching(base::apple::NSToCFPtrCast(query),
                                     nullptr) == errSecSuccess;
}

OSStatus SafeStorage::AdoptKey(std::string_view service,
                               std::string_view account) const {
  NSMutableDictionary* query = [GenericPassword(service, account) mutableCopy];
  query[base::apple::CFToNSPtrCast(kSecReturnData)] = @YES;
  base::apple::ScopedCFTypeRef<CFTypeRef> result;
  const OSStatus status = keychain_->ItemCopyMatching(
      base::apple::NSToCFPtrCast(query), result.InitializeInto());
  CFDataRef key = base::apple::CFCast<CFDataRef>(result.get());
  if (status != errSecSuccess || !key) {
    return status;
  }

  // Nothing that matters is encrypted with an earlier key of Pane's: the
  // migration runs only before the native app has profiles.
  NSDictionary* item = GenericPassword(KeychainPassword::GetServiceName(),
                                       KeychainPassword::GetAccountName());
  keychain_->ItemDelete(base::apple::NSToCFPtrCast(item));

  NSMutableDictionary* adopted = [item mutableCopy];
  adopted[base::apple::CFToNSPtrCast(kSecValueData)] =
      base::apple::CFToNSPtrCast(key);
  return keychain_->ItemAdd(base::apple::NSToCFPtrCast(adopted), nullptr);
}

OSStatus SafeStorage::DeleteKey(std::string_view service,
                                std::string_view account) const {
  LAContext* context = [[LAContext alloc] init];
  context.interactionNotAllowed = YES;

  NSMutableDictionary* query = [GenericPassword(service, account) mutableCopy];
  query[base::apple::CFToNSPtrCast(kSecUseAuthenticationContext)] = context;
  return keychain_->ItemDelete(base::apple::NSToCFPtrCast(query));
}

base::expected<std::vector<uint8_t>, OSStatus> SafeStorage::Find(
    std::string_view service,
    std::string_view account) const {
  return keychain_->FindGenericPassword(service, account);
}

}  // namespace pane::model
