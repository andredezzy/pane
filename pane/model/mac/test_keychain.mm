// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/test_keychain.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <map>
#include <utility>
#include <vector>

#include "base/apple/bridging.h"
#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/thread_annotations.h"
#include "base/threading/thread_restrictions.h"
#include "components/os_crypt/common/keychain_password_mac.h"
#include "crypto/apple/keychain_v2.h"

namespace pane::model {

class TestKeychain::Fake : public crypto::apple::KeychainV2 {
 public:
  Fake() = default;
  Fake(const Fake&) = delete;
  Fake& operator=(const Fake&) = delete;
  ~Fake() override = default;

  void Set(std::string_view service,
           std::string_view account,
           std::string_view bytes) {
    base::AutoLock lock(lock_);
    items_[Key(service, account)] = std::string(bytes);
  }

  std::optional<std::string> Get(std::string_view service,
                                 std::string_view account) const {
    base::AutoLock lock(lock_);
    const auto item = items_.find(Key(service, account));
    if (item == items_.end()) {
      return std::nullopt;
    }
    return item->second;
  }

  void FailWith(OSStatus status) {
    base::AutoLock lock(lock_);
    failure_ = status;
  }

  int reads() const {
    base::AutoLock lock(lock_);
    return reads_;
  }

  void HoldReadsUntil(base::WaitableEvent* gate) {
    base::AutoLock lock(lock_);
    gate_ = gate;
  }

  // KeychainV2:
  OSStatus ItemAdd(CFDictionaryRef attributes, CFTypeRef* result) override {
    base::AutoLock lock(lock_);
    if (failure_ != errSecSuccess) {
      return failure_;
    }
    const std::pair<std::string, std::string> key = KeyOf(attributes);
    if (items_.contains(key)) {
      return errSecDuplicateItem;
    }
    CFDataRef data = base::apple::GetValueFromDictionary<CFDataRef>(
        attributes, kSecValueData);
    items_[key] =
        data
            ? std::string(base::as_string_view(base::apple::CFDataToSpan(data)))
            : std::string();
    return errSecSuccess;
  }

  OSStatus ItemCopyMatching(CFDictionaryRef query, CFTypeRef* result) override {
    WaitForTheGate();
    base::AutoLock lock(lock_);
    if (failure_ != errSecSuccess) {
      return failure_;
    }
    const auto item = items_.find(KeyOf(query));
    if (item == items_.end()) {
      return errSecItemNotFound;
    }
    CFBooleanRef data = base::apple::GetValueFromDictionary<CFBooleanRef>(
        query, kSecReturnData);
    if (data && CFBooleanGetValue(data)) {
      ++reads_;
      if (result) {
        const base::span<const uint8_t> bytes =
            base::as_byte_span(item->second);
        *result = CFDataCreate(kCFAllocatorDefault, bytes.data(),
                               static_cast<CFIndex>(bytes.size()));
      }
    }
    return errSecSuccess;
  }

  OSStatus ItemDelete(CFDictionaryRef query) override {
    base::AutoLock lock(lock_);
    if (failure_ != errSecSuccess) {
      return failure_;
    }
    return items_.erase(KeyOf(query)) ? errSecSuccess : errSecItemNotFound;
  }

  OSStatus ItemUpdate(CFDictionaryRef query,
                      CFDictionaryRef keychain_data) override {
    return errSecUnimplemented;
  }

  OSStatus AddGenericPassword(std::string_view service_name,
                              std::string_view account_name,
                              base::span<const uint8_t> password) override {
    base::AutoLock lock(lock_);
    if (failure_ != errSecSuccess) {
      return failure_;
    }
    items_[Key(service_name, account_name)] =
        std::string(base::as_string_view(password));
    return errSecSuccess;
  }

  base::expected<std::vector<uint8_t>, OSStatus> FindGenericPassword(
      std::string_view service_name,
      std::string_view account_name) override {
    WaitForTheGate();
    base::AutoLock lock(lock_);
    if (failure_ != errSecSuccess) {
      return base::unexpected(failure_);
    }
    const auto item = items_.find(Key(service_name, account_name));
    if (item == items_.end()) {
      return base::unexpected(errSecItemNotFound);
    }
    ++reads_;
    return base::ToVector(base::as_byte_span(item->second));
  }

 private:
  // A read waits here while a test holds the Keychain's answers, the lock
  // let go meanwhile.
  void WaitForTheGate() {
    base::WaitableEvent* gate;
    {
      base::AutoLock lock(lock_);
      gate = gate_;
    }
    if (gate) {
      base::ScopedAllowBaseSyncPrimitivesForTesting allow;
      gate->Wait();
    }
  }

  static std::pair<std::string, std::string> Key(std::string_view service,
                                                 std::string_view account) {
    return {std::string(service), std::string(account)};
  }

  static std::pair<std::string, std::string> KeyOf(CFDictionaryRef query) {
    CFStringRef service = base::apple::GetValueFromDictionary<CFStringRef>(
        query, kSecAttrService);
    CFStringRef account = base::apple::GetValueFromDictionary<CFStringRef>(
        query, kSecAttrAccount);
    return {service ? base::SysCFStringRefToUTF8(service) : std::string(),
            account ? base::SysCFStringRefToUTF8(account) : std::string()};
  }

  mutable base::Lock lock_;
  std::map<std::pair<std::string, std::string>, std::string> items_
      GUARDED_BY(lock_);
  OSStatus failure_ GUARDED_BY(lock_) = errSecSuccess;
  int reads_ GUARDED_BY(lock_) = 0;
  raw_ptr<base::WaitableEvent> gate_ GUARDED_BY(lock_) = nullptr;
};

TestKeychain::TestKeychain() : fake_(std::make_unique<Fake>()) {}

TestKeychain::~TestKeychain() = default;

SafeStorage TestKeychain::safe_storage() {
  return SafeStorage(*fake_);
}

void TestKeychain::SetItem(std::string_view service,
                           std::string_view account,
                           std::string_view bytes) {
  fake_->Set(service, account, bytes);
}

std::optional<std::string> TestKeychain::Item(std::string_view service,
                                              std::string_view account) const {
  return fake_->Get(service, account);
}

void TestKeychain::SetEngineSecret(std::string_view bytes) {
  SetItem(KeychainPassword::GetServiceName(),
          KeychainPassword::GetAccountName(), bytes);
}

std::optional<std::string> TestKeychain::EngineSecret() const {
  return Item(KeychainPassword::GetServiceName(),
              KeychainPassword::GetAccountName());
}

void TestKeychain::FailWith(OSStatus status) {
  fake_->FailWith(status);
}

void TestKeychain::HoldReadsUntil(base::WaitableEvent* gate) {
  fake_->HoldReadsUntil(gate);
}

int TestKeychain::reads() const {
  return fake_->reads();
}

}  // namespace pane::model
