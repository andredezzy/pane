// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/pin_guard.h"

#include <windows.h>

#include <ncrypt.h>
#include <winerror.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "crypto/hash.h"
#include "crypto/random.h"
#include "crypto/scoped_cng_types.h"

namespace pane::model {

namespace {

// Every key of a data folder's is named by it: "Pane data key <8 bytes of the
// folder's SHA-256> <8 random bytes>", so a reseal and the panic wipe find
// them all, and a test copy's never meet the user's.
constexpr std::string_view kKeyName = "Pane data key ";

// The RSA key and its padding, as the provider takes them on TPM 2.0 (probed
// in the test VM, 2026-11-13).
constexpr DWORD kKeyBits = 2048;
constexpr size_t kShareSize = kKeySize;

std::string PrefixFor(const base::FilePath& data_directory) {
  // NTFS names compare without case: another spelling of the same folder
  // names the same keys.
  const std::array<uint8_t, crypto::hash::kSha256Size> digest =
      crypto::hash::Sha256(base::ToLowerASCII(data_directory.AsUTF8Unsafe()));
  return base::StrCat(
      {kKeyName, base::HexEncodeLower(base::span(digest).first<8u>()), " "});
}

// Whether a status is a lockout's: the chip's (NTE_AUTHENTICATION_IGNORED), or
// Windows' own for a standard user, as Pane runs, which refuses that user's
// authorizations after 4 failures in its window (default 8 hours) before the
// chip's 31 (TPM_E_PCP_AUTHENTICATION_IGNORED from the provider; probed in the
// test VM, 2026-11-13).
bool IsLockout(SECURITY_STATUS status) {
  return status == NTE_AUTHENTICATION_IGNORED ||
         status == TPM_E_PCP_AUTHENTICATION_IGNORED;
}

// What a status the provider returned means for the PIN. Probed: a wrong PIN
// is NTE_PERM, counted by the chip and by Windows; a lockout refuses the
// right PIN too; a deleted key is NTE_BAD_KEYSET.
PinGuard::Refusal RefusalOf(SECURITY_STATUS status) {
  if (IsLockout(status)) {
    return PinGuard::Refusal::kLockedOut;
  }
  switch (status) {
    case NTE_PERM:
    case TPM_E_PCP_AUTHENTICATION_FAILED:
      return PinGuard::Refusal::kWrongPin;
    default:
      return PinGuard::Refusal::kGone;
  }
}

// The PIN as the provider takes it: UTF-16 with its terminating null.
std::wstring WidePin(std::string_view pin) {
  return base::UTF8ToWide(pin);
}

SECURITY_STATUS SetPin(NCRYPT_HANDLE handle, const std::wstring& pin) {
  return NCryptSetProperty(
      handle, NCRYPT_PIN_PROPERTY,
      reinterpret_cast<PBYTE>(const_cast<wchar_t*>(pin.c_str())),
      static_cast<DWORD>((pin.size() + 1) * sizeof(wchar_t)), 0);
}

SECURITY_STATUS SetDword(NCRYPT_HANDLE handle, LPCWSTR property, DWORD value) {
  return NCryptSetProperty(handle, property, reinterpret_cast<PBYTE>(&value),
                           sizeof(value), 0);
}

BCRYPT_OAEP_PADDING_INFO Oaep() {
  return {
      .pszAlgId = BCRYPT_SHA256_ALGORITHM, .pbLabel = nullptr, .cbLabel = 0};
}

class TpmGuard : public PinGuard {
 public:
  explicit TpmGuard(std::string prefix) : prefix_(std::move(prefix)) {}

  base::expected<Guarded, Refusal> Guard(std::string_view pin) override {
    crypto::ScopedNCryptProvider provider = Provider();
    if (!provider.is_valid()) {
      return base::unexpected(Refusal::kUnavailable);
    }
    const std::string name = base::StrCat(
        {prefix_, base::HexEncodeLower(crypto::RandBytesAsArray<8>())});
    const std::wstring wide_name = base::UTF8ToWide(name);

    crypto::ScopedNCryptKey key;
    SECURITY_STATUS status = NCryptCreatePersistedKey(
        provider.get(), crypto::ScopedNCryptKey::Receiver(key).get(),
        BCRYPT_RSA_ALGORITHM, wide_name.c_str(), /*dwLegacyKeySpec=*/0,
        /*dwFlags=*/0);
    if (FAILED(status)) {
      LOG(ERROR) << "The TPM made no key: " << std::hex << status;
      return base::unexpected(Refusal::kUnavailable);
    }
    // The PIN is the key's own, set before the chip makes it, silently: no
    // Windows UI asks for it, then or at a use.
    const std::wstring wide_pin = WidePin(pin);
    if (FAILED(status =
                   SetDword(key.get(), NCRYPT_LENGTH_PROPERTY, kKeyBits)) ||
        FAILED(status = SetDword(key.get(), NCRYPT_KEY_USAGE_PROPERTY,
                                 NCRYPT_ALLOW_DECRYPT_FLAG)) ||
        FAILED(status = SetPin(key.get(), wide_pin)) ||
        FAILED(status = NCryptFinalizeKey(key.get(), NCRYPT_SILENT_FLAG))) {
      LOG(ERROR) << "The TPM made no key: " << std::hex << status;
      // Deleting it frees the handle, made or not.
      NCryptDeleteKey(key.release(), 0);
      // A lockout refuses the provider's own authorization too: the chip is
      // there, and makes keys again once it is over.
      return base::unexpected(IsLockout(status) ? Refusal::kLockedOut
                                                : Refusal::kUnavailable);
    }

    Guarded guarded;
    guarded.share = crypto::RandBytesAsArray<kShareSize>();
    BCRYPT_OAEP_PADDING_INFO padding = Oaep();
    DWORD size = 0;
    status = NCryptEncrypt(key.get(), guarded.share.data(),
                           static_cast<DWORD>(guarded.share.size()), &padding,
                           nullptr, 0, &size,
                           NCRYPT_PAD_OAEP_FLAG | NCRYPT_SILENT_FLAG);
    std::vector<uint8_t> sealed(size);
    if (SUCCEEDED(status)) {
      status = NCryptEncrypt(key.get(), guarded.share.data(),
                             static_cast<DWORD>(guarded.share.size()), &padding,
                             sealed.data(), static_cast<DWORD>(sealed.size()),
                             &size, NCRYPT_PAD_OAEP_FLAG | NCRYPT_SILENT_FLAG);
    }
    if (FAILED(status)) {
      LOG(ERROR) << "The TPM's key sealed no share: " << std::hex << status;
      NCryptDeleteKey(key.release(), 0);
      return base::unexpected(Refusal::kUnavailable);
    }
    sealed.resize(size);
    guarded.kept = {.key = name, .share = base::Base64Encode(sealed)};
    return guarded;
  }

  base::expected<Key, Refusal> Unseal(const GuardedShare& kept,
                                      std::string_view pin) override {
    const std::optional<std::vector<uint8_t>> sealed =
        base::Base64Decode(kept.share);
    if (!sealed || sealed->empty()) {
      return base::unexpected(Refusal::kGone);
    }
    crypto::ScopedNCryptProvider provider = Provider();
    if (!provider.is_valid()) {
      return base::unexpected(Refusal::kUnavailable);
    }
    const std::wstring name = base::UTF8ToWide(kept.key);
    crypto::ScopedNCryptKey key;
    SECURITY_STATUS status = NCryptOpenKey(
        provider.get(), crypto::ScopedNCryptKey::Receiver(key).get(),
        name.c_str(), /*dwLegacyKeySpec=*/0, NCRYPT_SILENT_FLAG);
    if (FAILED(status)) {
      // NTE_BAD_KEYSET: deleted, as the wipe deletes it, or never here.
      LOG(ERROR) << "The TPM's key would not open: " << std::hex << status;
      return base::unexpected(Refusal::kGone);
    }
    if (FAILED(status = SetPin(key.get(), WidePin(pin)))) {
      LOG(ERROR) << "The TPM's key took no PIN: " << std::hex << status;
      return base::unexpected(Refusal::kGone);
    }

    BCRYPT_OAEP_PADDING_INFO padding = Oaep();
    std::vector<uint8_t> share(sealed->size());
    DWORD size = 0;
    status = NCryptDecrypt(key.get(), const_cast<PBYTE>(sealed->data()),
                           static_cast<DWORD>(sealed->size()), &padding,
                           share.data(), static_cast<DWORD>(share.size()),
                           &size, NCRYPT_PAD_OAEP_FLAG | NCRYPT_SILENT_FLAG);
    if (FAILED(status)) {
      const Refusal refusal = RefusalOf(status);
      if (refusal == Refusal::kGone) {
        LOG(ERROR) << "The TPM gave no share: " << std::hex << status;
      }
      return base::unexpected(refusal);
    }
    if (size != kShareSize) {
      return base::unexpected(Refusal::kGone);
    }
    Key released;
    std::ranges::copy(base::span(share).first<kShareSize>(), released.begin());
    return released;
  }

  void Forget(std::optional<std::string_view> kept) override {
    crypto::ScopedNCryptProvider provider = Provider();
    if (!provider.is_valid()) {
      return;
    }
    // Named first, then deleted: the list isn't changed while it is read.
    std::vector<std::wstring> names;
    NCryptKeyName* item = nullptr;
    PVOID state = nullptr;
    while (NCryptEnumKeys(provider.get(), /*pszScope=*/nullptr, &item, &state,
                          NCRYPT_SILENT_FLAG) == ERROR_SUCCESS) {
      const std::string name = base::WideToUTF8(item->pszName);
      if (name.starts_with(prefix_) && (!kept || name != *kept)) {
        names.push_back(item->pszName);
      }
      NCryptFreeBuffer(item);
    }
    if (state) {
      NCryptFreeBuffer(state);
    }

    for (const std::wstring& name : names) {
      crypto::ScopedNCryptKey key;
      SECURITY_STATUS status = NCryptOpenKey(
          provider.get(), crypto::ScopedNCryptKey::Receiver(key).get(),
          name.c_str(), /*dwLegacyKeySpec=*/0, NCRYPT_SILENT_FLAG);
      // Deleting frees the handle, whatever comes of it.
      if (SUCCEEDED(status)) {
        status = NCryptDeleteKey(key.release(), /*dwFlags=*/0);
      }
      if (FAILED(status)) {
        LOG(ERROR) << "Could not delete one of the TPM's keys: " << std::hex
                   << status;
      }
    }
  }

 private:
  ~TpmGuard() override = default;

  static crypto::ScopedNCryptProvider Provider() {
    crypto::ScopedNCryptProvider provider;
    const SECURITY_STATUS status = NCryptOpenStorageProvider(
        crypto::ScopedNCryptProvider::Receiver(provider).get(),
        MS_PLATFORM_CRYPTO_PROVIDER, /*dwFlags=*/0);
    if (FAILED(status)) {
      LOG(ERROR) << "No TPM 2.0 provider: " << std::hex << status;
      return crypto::ScopedNCryptProvider();
    }
    return provider;
  }

  const std::string prefix_;
};

}  // namespace

// static
scoped_refptr<PinGuard> PinGuard::ForData(
    const base::FilePath& data_directory) {
  return base::MakeRefCounted<TpmGuard>(PrefixFor(data_directory));
}

}  // namespace pane::model
