// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/stream_cipher.h"

#include <algorithm>
#include <string_view>

#include "base/check.h"
#include "base/check_op.h"
#include "base/containers/span.h"
#include "base/memory/ptr_util.h"
#include "base/numerics/byte_conversions.h"
#include "third_party/boringssl/src/include/openssl/digest.h"
#include "third_party/boringssl/src/include/openssl/hmac.h"
#include "third_party/boringssl/src/include/openssl/mem.h"
#include "third_party/boringssl/src/include/openssl/rand.h"

namespace pane::encryption {

namespace {

// Magic and version (8), the file id (16), and the check (8).
constexpr std::string_view kMagic = "PANESF1\n";
constexpr size_t kMagicSize = 8;
constexpr size_t kFileIDSize = 16;
constexpr size_t kCheckSize = 8;
constexpr size_t kBlockSize = 16;

std::array<uint8_t, kCheckSize> CheckOf(const ProfileKeys& keys,
                                        base::span<const uint8_t> file_id) {
  uint8_t digest[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  CHECK(HMAC(EVP_sha256(), keys.stream_check.data(), keys.stream_check.size(),
             file_id.data(), file_id.size(), digest, &length));
  std::array<uint8_t, kCheckSize> check;
  base::span(check).copy_from(base::span(digest).first<kCheckSize>());
  OPENSSL_cleanse(digest, sizeof(digest));
  return check;
}

Key FileKey(const ProfileKeys& keys, base::span<const uint8_t> file_id) {
  return DeriveKey(keys.stream_files, file_id, "Pane stream file");
}

}  // namespace

// static
std::unique_ptr<StreamCipher> StreamCipher::Under(
    const ProfileKeys& keys,
    base::span<const uint8_t> file_id) {
  Key file_key = FileKey(keys, file_id);
  auto cipher = base::WrapUnique(new StreamCipher(file_key));
  OPENSSL_cleanse(file_key.data(), file_key.size());
  return cipher;
}

StreamCipher::StreamCipher(base::span<const uint8_t, kKeySize> file_key) {
  CHECK_EQ(AES_set_encrypt_key(file_key.data(), kKeySize * 8, &key_), 0);
}

StreamCipher::~StreamCipher() {
  OPENSSL_cleanse(&key_, sizeof(key_));
}

// static
std::unique_ptr<StreamCipher> StreamCipher::ForNewFile(const ProfileKeys& keys,
                                                       Header* header) {
  base::span<uint8_t> out(*header);
  out.first<kMagicSize>().copy_from(base::as_byte_span(kMagic).first<kMagicSize>());
  auto file_id = out.subspan(kMagicSize, kFileIDSize);
  RAND_bytes(file_id.data(), file_id.size());
  out.last<kCheckSize>().copy_from(CheckOf(keys, file_id));

  return Under(keys, file_id);
}

// static
bool StreamCipher::HasMagic(base::span<const uint8_t> stored) {
  return stored.size() >= kMagicSize &&
         stored.first<kMagicSize>() ==
             base::as_byte_span(kMagic).first<kMagicSize>();
}

// static
bool StreamCipher::HasMagicStart(base::span<const uint8_t> stored) {
  const size_t size = std::min(stored.size(), kMagicSize);
  return stored.first(size) == base::as_byte_span(kMagic).first(size);
}

// static
std::unique_ptr<StreamCipher> StreamCipher::FromHeader(
    const ProfileKeys& keys,
    base::span<const uint8_t, kHeaderSize> header) {
  CHECK(HasMagic(header)) << "A profile's stream file with no header.";
  auto file_id = base::span<const uint8_t>(header).subspan(kMagicSize, kFileIDSize);
  auto check = base::span<const uint8_t>(header).last<kCheckSize>();
  CHECK(check == base::span<const uint8_t>(CheckOf(keys, file_id)))
      << "A profile's stream file under another key.";

  return Under(keys, file_id);
}

void StreamCipher::Apply(uint64_t offset,
                         base::span<const uint8_t> in,
                         base::span<uint8_t> out) const {
  CHECK_EQ(in.size(), out.size());

  std::array<uint8_t, kBlockSize> counter = {};
  base::span(counter).last<8u>().copy_from(
      base::U64ToBigEndian(offset / kBlockSize));

  uint8_t keystream[kBlockSize] = {};
  unsigned int used = static_cast<unsigned int>(offset % kBlockSize);

  // Partway into a block: its keystream first, as CTR's own state would hold.
  if (used != 0) {
    AES_encrypt(counter.data(), keystream, &key_);
    base::span(counter).last<8u>().copy_from(
        base::U64ToBigEndian(offset / kBlockSize + 1));
  }

  AES_ctr128_encrypt(in.data(), out.data(), in.size(), &key_, counter.data(),
                     keystream, &used);
  OPENSSL_cleanse(keystream, sizeof(keystream));
}

}  // namespace pane::encryption
