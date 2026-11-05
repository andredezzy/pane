// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_ENCRYPTION_STREAM_CIPHER_H_
#define PANE_ENCRYPTION_STREAM_CIPHER_H_

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <memory>

#include "base/containers/span.h"
#include "pane/encryption/engine_keys.h"
#include "third_party/boringssl/src/include/openssl/aes.h"

namespace pane::encryption {

// A file written front to back and never changed in place: LevelDB's files,
// IndexedDB's blobs, the origin private file system's. 32 bytes of header (magic
// and version, flags, a random file id, and a check of the key), then the
// content under AES-256-CTR, the counter being the logical block index, under a
// key of the file's own. A file fails for good after any failed write, so its
// keystream is never reused.
class StreamCipher {
 public:
  static constexpr size_t kHeaderSize = 32;
  using Header = std::array<uint8_t, kHeaderSize>;

  // A new file's cipher, and the header to write first.
  static std::unique_ptr<StreamCipher> ForNewFile(const ProfileKeys& keys,
                                                  Header* header);

  // True for what a new file's header starts with.
  static bool HasMagic(base::span<const uint8_t> stored);

  // An existing file's cipher. Crashes on a header made under another key: an
  // error here would have Chromium repair or delete the store.
  static std::unique_ptr<StreamCipher> FromHeader(
      const ProfileKeys& keys,
      base::span<const uint8_t, kHeaderSize> header);

  StreamCipher(const StreamCipher&) = delete;
  StreamCipher& operator=(const StreamCipher&) = delete;
  ~StreamCipher();

  // Encrypts or decrypts `in` into `out` (the same size; they may be the same
  // memory), the bytes at `offset` of the file's content. Safe from any thread.
  void Apply(uint64_t offset,
             base::span<const uint8_t> in,
             base::span<uint8_t> out) const;

 private:
  explicit StreamCipher(base::span<const uint8_t, kKeySize> file_key);

  AES_KEY key_;
};

}  // namespace pane::encryption

#endif  // PANE_ENCRYPTION_STREAM_CIPHER_H_
