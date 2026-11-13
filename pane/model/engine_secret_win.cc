// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/engine_secret.h"

#include <windows.h>

#include <dpapi.h>

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "base/base64.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/values.h"
#include "base/win/scoped_localalloc.h"
#include "crypto/random.h"
#include "pane/model/persisted_file.h"
#include "pane/model/test_hooks.h"

namespace pane::model {

namespace {

// Chromium's names and format for the key (os_crypt_win.cc,
// dpapi_key_provider.cc).
constexpr char kEncryptedKeyPref[] = "encrypted_key";
constexpr char kAuditEnabledPref[] = "audit_enabled";
constexpr std::string_view kDpapiPrefix = "DPAPI";
constexpr size_t kSecretSize = 32;

bool IsMockKeychain() {
  return base::CommandLine::InitializedForCurrentProcess() &&
         base::CommandLine::ForCurrentProcess()->HasSwitch("use-mock-keychain");
}

std::optional<std::vector<uint8_t>> Unprotect(base::span<const uint8_t> blob) {
  DATA_BLOB input = {.cbData = static_cast<DWORD>(blob.size()),
                     .pbData = const_cast<BYTE*>(blob.data())};
  DATA_BLOB output = {};
  if (!::CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0,
                            &output)) {
    PLOG(ERROR) << "DPAPI would not open the engine's key";
    return std::nullopt;
  }
  const base::win::ScopedLocalAlloc owned =
      base::win::TakeLocalAlloc(output.pbData);
  // SAFETY: DPAPI gives `cbData` bytes at `pbData`.
  return base::ToVector(UNSAFE_BUFFERS(
      base::span(static_cast<const uint8_t*>(owned.get()), output.cbData)));
}

std::optional<std::vector<uint8_t>> Protect(base::span<const uint8_t> secret) {
  DATA_BLOB input = {.cbData = static_cast<DWORD>(secret.size()),
                     .pbData = const_cast<BYTE*>(secret.data())};
  DATA_BLOB output = {};
  // As Chromium seals it: no prompt, and audited.
  if (!::CryptProtectData(&input, L"Pane", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_AUDIT, &output)) {
    PLOG(ERROR) << "DPAPI would not seal the engine's key";
    return std::nullopt;
  }
  const base::win::ScopedLocalAlloc owned =
      base::win::TakeLocalAlloc(output.pbData);
  // SAFETY: DPAPI gives `cbData` bytes at `pbData`.
  return base::ToVector(UNSAFE_BUFFERS(
      base::span(static_cast<const uint8_t*>(owned.get()), output.cbData)));
}

// The key Local State holds, opened; nullopt when it holds none, or none that
// opens.
std::optional<std::vector<uint8_t>> Read(const base::DictValue& state,
                                         bool& present) {
  const base::DictValue* os_crypt = state.FindDict("os_crypt");
  const std::string* encoded =
      os_crypt ? os_crypt->FindString(kEncryptedKeyPref) : nullptr;
  present = encoded && !encoded->empty();
  if (!present) {
    return std::nullopt;
  }
  const std::optional<std::vector<uint8_t>> decoded =
      base::Base64Decode(*encoded);
  if (!decoded || decoded->size() <= kDpapiPrefix.size() ||
      base::span(*decoded).first(kDpapiPrefix.size()) !=
          base::as_byte_span(kDpapiPrefix)) {
    LOG(ERROR) << "The engine's key is not DPAPI's";
    return std::nullopt;
  }
  std::optional<std::vector<uint8_t>> secret =
      Unprotect(base::span(*decoded).subspan(kDpapiPrefix.size()));
  if (secret && secret->size() != kSecretSize) {
    LOG(ERROR) << "The engine's key is not 256 bits";
    return std::nullopt;
  }
  return secret;
}

}  // namespace

std::optional<std::vector<uint8_t>> EngineSecret(
    const base::FilePath& user_data_directory,
    bool makes_one) {
  if (IsMockKeychain()) {
    if (TestHook("PANE_MOCK_KEYCHAIN_DENIES")) {
      return std::nullopt;
    }
    return base::ToVector(base::as_byte_span(kMockKeychainSecret));
  }

  const base::FilePath file =
      user_data_directory.Append(FILE_PATH_LITERAL("Local State"));
  base::expected<std::optional<base::Value>, FileError> read =
      ReadJsonFile(file);
  if (!read.has_value()) {
    LOG(ERROR) << "Could not read Local State";
    return std::nullopt;
  }
  base::DictValue state;
  if (*read) {
    if (!(*read)->is_dict()) {
      LOG(ERROR) << "Local State is not an object";
      return std::nullopt;
    }
    state = std::move(**read).TakeDict();
  }

  bool present = false;
  std::optional<std::vector<uint8_t>> secret = Read(state, present);
  // One that won't open stays as it is: the lock says the key can't be read,
  // where a new one would leave the data key sealed under nothing there.
  if (secret || present || !makes_one) {
    return secret;
  }

  // Made as Chromium makes it, which then reads it as its own
  // (os_crypt_async::InitWithExistingKey).
  const std::array<uint8_t, kSecretSize> made =
      crypto::RandBytesAsArray<kSecretSize>();
  const std::optional<std::vector<uint8_t>> sealed = Protect(made);
  if (!sealed) {
    return std::nullopt;
  }
  std::vector<uint8_t> blob(kDpapiPrefix.begin(), kDpapiPrefix.end());
  blob.insert(blob.end(), sealed->begin(), sealed->end());
  base::DictValue* os_crypt = state.EnsureDict("os_crypt");
  os_crypt->Set(kEncryptedKeyPref, base::Base64Encode(blob));
  os_crypt->Set(kAuditEnabledPref, true);
  if (!WriteJsonFile(file, base::Value(std::move(state)))) {
    LOG(ERROR) << "Could not write the engine's key into Local State";
    return std::nullopt;
  }
  return base::ToVector(made);
}

}  // namespace pane::model
