// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/safe_storage.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <string>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "components/os_crypt/common/keychain_password_mac.h"
#include "components/os_crypt/common/os_crypt_switches.h"
#include "crypto/apple/scoped_keychain_user_interaction_allowed.h"
#include "pane/model/mac/keychain_v2_calls.h"

namespace pane::model {

namespace {

base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> GenericPassword(
    std::string_view service,
    std::string_view account) {
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> query(
      CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks));
  CFDictionarySetValue(query.get(), kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query.get(), kSecAttrService,
                       base::SysUTF8ToCFStringRef(service).get());
  CFDictionarySetValue(query.get(), kSecAttrAccount,
                       base::SysUTF8ToCFStringRef(account).get());
  return query;
}

}  // namespace

// static
SafeStorage SafeStorage::ForProcess() {
  if (UsesMockKeychain()) {
    return Mock();
  }
  return SafeStorage(LoginKeychain());
}

// static
bool SafeStorage::UsesMockKeychain() {
  return base::CommandLine::InitializedForCurrentProcess() &&
         base::CommandLine::ForCurrentProcess()->HasSwitch(
             os_crypt::switches::kUseMockKeychain);
}

// static
SafeStorage SafeStorage::Mock() {
  return SafeStorage(MockKeychainV2(), /*mock=*/true);
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
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> query = GenericPassword(
      KeychainPassword::GetServiceName(), KeychainPassword::GetAccountName());
  CFDictionarySetValue(query.get(), kSecReturnAttributes, kCFBooleanTrue);
  return ItemCopyMatching(*keychain_, query.get(), nullptr) == errSecSuccess;
}

OSStatus SafeStorage::AdoptKey(std::string_view service,
                               std::string_view account) const {
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> query =
      GenericPassword(service, account);
  CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
  base::apple::ScopedCFTypeRef<CFTypeRef> result;
  const OSStatus status =
      ItemCopyMatching(*keychain_, query.get(), result.InitializeInto());
  CFDataRef key = base::apple::CFCast<CFDataRef>(result.get());
  if (status != errSecSuccess || !key) {
    return status;
  }

  // Nothing that matters is encrypted with an earlier key of Pane's: the
  // migration runs only before the native app has profiles.
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> item = GenericPassword(
      KeychainPassword::GetServiceName(), KeychainPassword::GetAccountName());
  ItemDelete(*keychain_, item.get());

  CFDictionarySetValue(item.get(), kSecValueData, key);
  return ItemAdd(*keychain_, item.get(), nullptr);
}

// Never a prompt, for the panic wipe deletes the Electron app's key with it
// (wipe_sources.cc): one would wait mid-wipe for a password nobody types. An
// LAContext's interactionNotAllowed keeps it off a data protection
// keychain's item, and SecKeychainSetUserInteractionAllowed, Chromium's
// scope, off the login Keychain's, a file-based keychain where the former
// doesn't hold (FB16959400, scoped_keychain_user_interaction_allowed.cc):
// where it would have asked, the delete fails, and the wipe goes on.
OSStatus SafeStorage::DeleteKey(std::string_view service,
                                std::string_view account) const {
  crypto::apple::ScopedKeychainUserInteractionAllowed no_prompt(false);
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> query =
      GenericPassword(service, account);
  CFDictionarySetValue(query.get(), kSecUseAuthenticationContext,
                       ContextThatNeverAsks().get());
  return ItemDelete(*keychain_, query.get());
}

base::expected<std::vector<uint8_t>, OSStatus> SafeStorage::Find(
    std::string_view service,
    std::string_view account) const {
  return FindGenericPassword(*keychain_, service, account);
}

}  // namespace pane::model
