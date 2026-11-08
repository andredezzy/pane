// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/whole_file.h"

#include <string_view>

#include "base/check.h"
#include "base/check_op.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/pane_file_codec.h"
#include "third_party/boringssl/src/include/openssl/aead.h"
#include "third_party/boringssl/src/include/openssl/rand.h"

namespace pane::encryption {

namespace {

constexpr std::string_view kMagic = "PANEWF1\n";
constexpr size_t kNonceSize = 12;
constexpr size_t kTagSize = 16;

base::span<const uint8_t> MagicBytes() {
  return base::as_byte_span(kMagic);
}

bool SealForPath(const base::FilePath& path,
                 base::span<const uint8_t> plaintext,
                 std::string* sealed) {
  std::optional<ProfileKeys> keys = KeysFor(path);

  if (!keys) {
    return false;
  }

  *sealed = SealWholeFile(*keys, plaintext);
  return true;
}

// A plain file in a protected path passes as it is: reading one risks nothing,
// and crashing on one would stop a profile for a file Chromium can rebuild.
bool OpenForPath(const base::FilePath& path,
                 base::span<const uint8_t> stored,
                 std::string* plaintext) {
  if (!IsSealedWholeFile(stored)) {
    return false;
  }

  std::optional<ProfileKeys> keys = KeysFor(path);

  if (!keys) {
    return false;
  }

  std::optional<std::string> opened = OpenWholeFile(*keys, stored);

  // Another key, or changed bytes: an error here would have Chromium start over
  // and overwrite the file. A crash loses nothing.
  CHECK(opened) << "A profile's file that does not open.";

  *plaintext = std::move(*opened);
  return true;
}

constexpr base::PaneFileCodec kCodec = {
    .seal = &SealForPath,
    .open = &OpenForPath,
};

}  // namespace

std::string SealWholeFile(const ProfileKeys& keys,
                          base::span<const uint8_t> plaintext) {
  bssl::ScopedEVP_AEAD_CTX context;
  CHECK(EVP_AEAD_CTX_init(context.get(), EVP_aead_aes_256_gcm(),
                          keys.whole_files.data(), keys.whole_files.size(),
                          kTagSize, nullptr));

  std::string sealed(kMagic.size() + kNonceSize + plaintext.size() + kTagSize,
                     '\0');
  base::span<uint8_t> out = base::as_writable_byte_span(sealed);
  auto [magic, rest] = out.split_at(kMagic.size());
  magic.copy_from(MagicBytes());
  auto [nonce, body] = rest.split_at(kNonceSize);
  RAND_bytes(nonce.data(), nonce.size());

  size_t written = 0;
  CHECK(EVP_AEAD_CTX_seal(context.get(), body.data(), &written, body.size(),
                          nonce.data(), nonce.size(), plaintext.data(),
                          plaintext.size(), MagicBytes().data(),
                          MagicBytes().size()));
  CHECK_EQ(written, body.size());
  return sealed;
}

bool IsSealedWholeFile(base::span<const uint8_t> stored) {
  return stored.size() >= kMagic.size() + kNonceSize + kTagSize &&
         stored.first(kMagic.size()) == MagicBytes();
}

std::optional<std::string> OpenWholeFile(const ProfileKeys& keys,
                                         base::span<const uint8_t> stored) {
  if (!IsSealedWholeFile(stored)) {
    return std::nullopt;
  }

  bssl::ScopedEVP_AEAD_CTX context;
  CHECK(EVP_AEAD_CTX_init(context.get(), EVP_aead_aes_256_gcm(),
                          keys.whole_files.data(), keys.whole_files.size(),
                          kTagSize, nullptr));

  auto nonce = stored.subspan(kMagic.size(), kNonceSize);
  auto body = stored.subspan(kMagic.size() + kNonceSize);
  std::string plaintext(body.size() - kTagSize, '\0');
  base::span<uint8_t> out = base::as_writable_byte_span(plaintext);
  size_t written = 0;

  if (!EVP_AEAD_CTX_open(context.get(), out.data(), &written, out.size(),
                         nonce.data(), nonce.size(), body.data(), body.size(),
                         MagicBytes().data(), MagicBytes().size())) {
    return std::nullopt;
  }

  return plaintext;
}

void InstallWholeFileCodec() {
  base::SetPaneFileCodec(&kCodec);
}

}  // namespace pane::encryption
