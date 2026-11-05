// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/stream_cipher.h"

#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/test/gtest_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::encryption {
namespace {

ProfileKeys KeysOf(uint8_t seed) {
  ProfileKeys keys;
  keys.stream_files.fill(seed);
  keys.stream_check.fill(seed + 1);
  return keys;
}

std::vector<uint8_t> Bytes(size_t size) {
  std::vector<uint8_t> bytes(size);
  for (size_t i = 0; i < size; i++) {
    bytes[i] = static_cast<uint8_t>(i * 31);
  }
  return bytes;
}

TEST(StreamCipherTest, AnyOffsetDecryptsWhatTheWholeWrite) {
  StreamCipher::Header header;
  auto writer = StreamCipher::ForNewFile(KeysOf(1), &header);
  const std::vector<uint8_t> plain = Bytes(4099);
  std::vector<uint8_t> stored(plain.size());
  writer->Apply(0, plain, stored);
  EXPECT_NE(stored, plain);

  auto reader = StreamCipher::FromHeader(KeysOf(1), header);

  for (const auto& [offset, size] :
       std::vector<std::pair<size_t, size_t>>{{0, 4099},
                                              {1, 15},
                                              {15, 1},
                                              {16, 16},
                                              {17, 4000},
                                              {4095, 4}}) {
    std::vector<uint8_t> out(size);
    reader->Apply(offset, base::span(stored).subspan(offset, size), out);
    EXPECT_EQ(out, std::vector<uint8_t>(plain.begin() + offset,
                                        plain.begin() + offset + size))
        << offset << " " << size;
  }
}

TEST(StreamCipherTest, PiecewiseAppendsMatchOneWrite) {
  StreamCipher::Header header;
  auto cipher = StreamCipher::ForNewFile(KeysOf(1), &header);
  const std::vector<uint8_t> plain = Bytes(1000);
  std::vector<uint8_t> whole(plain.size());
  cipher->Apply(0, plain, whole);

  std::vector<uint8_t> pieces(plain.size());
  size_t offset = 0;

  for (const size_t size : {1u, 7u, 16u, 33u, 500u, 443u}) {
    cipher->Apply(offset, base::span(plain).subspan(offset, size),
                  base::span(pieces).subspan(offset, size));
    offset += size;
  }

  EXPECT_EQ(offset, plain.size());
  EXPECT_EQ(pieces, whole);
}

TEST(StreamCipherTest, TwoFilesHaveDifferentKeystreams) {
  StreamCipher::Header one;
  StreamCipher::Header two;
  auto first = StreamCipher::ForNewFile(KeysOf(1), &one);
  auto second = StreamCipher::ForNewFile(KeysOf(1), &two);
  const std::vector<uint8_t> plain(64, 0);
  std::vector<uint8_t> a(64);
  std::vector<uint8_t> b(64);
  first->Apply(0, plain, a);
  second->Apply(0, plain, b);
  EXPECT_NE(a, b);
  EXPECT_NE(one, two);
}

TEST(StreamCipherTest, InPlace) {
  StreamCipher::Header header;
  auto cipher = StreamCipher::ForNewFile(KeysOf(1), &header);
  const std::vector<uint8_t> plain = Bytes(100);
  std::vector<uint8_t> buffer = plain;
  cipher->Apply(3, buffer, buffer);
  cipher->Apply(3, buffer, buffer);
  EXPECT_EQ(buffer, plain);
}

TEST(StreamCipherTest, AHeaderUnderAnotherKeyCrashes) {
  StreamCipher::Header header;
  StreamCipher::ForNewFile(KeysOf(1), &header);
  EXPECT_TRUE(StreamCipher::HasMagic(header));
  EXPECT_DEATH_IF_SUPPORTED(StreamCipher::FromHeader(KeysOf(9), header), "");
}

}  // namespace
}  // namespace pane::encryption
