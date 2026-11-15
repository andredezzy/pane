// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/payload.h"

#include <stdint.h>

#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/numerics/byte_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::installer {

namespace {

// A program's first bytes, enough of a PE header for its security
// directory: `certificate` at `certificate_size` where it names one.
std::vector<uint8_t> ProgramStub(uint32_t certificate = 0,
                                 uint32_t certificate_size = 0) {
  constexpr size_t kPe = 0x80;
  std::vector<uint8_t> bytes(0x200, 0);
  bytes[0] = 'M';
  bytes[1] = 'Z';
  base::span(bytes).subspan<0x3Cu, 4u>().copy_from(
      base::U32ToLittleEndian(kPe));
  bytes[kPe] = 'P';
  bytes[kPe + 1] = 'E';
  const size_t optional = kPe + 24;
  base::span(bytes).subspan(optional).first<2u>().copy_from(
      base::U16ToLittleEndian(0x20B));
  base::span(bytes)
      .subspan(optional + 144)
      .first<4u>()
      .copy_from(base::U32ToLittleEndian(certificate));
  base::span(bytes)
      .subspan(optional + 148)
      .first<4u>()
      .copy_from(base::U32ToLittleEndian(certificate_size));
  return bytes;
}

Payload PayloadAfter(size_t program, size_t archive) {
  Payload payload;
  payload.offset = program;
  payload.size = archive;
  payload.unpacked_size = 494198542;
  payload.sha256.fill(0xAB);
  payload.version = "0.21.0";
  return payload;
}

base::File Written(const base::FilePath& path,
                   const std::vector<uint8_t>& bytes) {
  EXPECT_TRUE(base::WriteFile(path, bytes));
  return base::File(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
}

}  // namespace

TEST(PayloadTest, TrailerReadsBack) {
  const Payload payload = PayloadAfter(4096, 1000);
  const auto trailer = TrailerFor(payload);
  EXPECT_EQ(ParseTrailer(trailer, 4096 + 1000 + kTrailerSize), payload);
}

TEST(PayloadTest, RefusesWhatIsNoTrailer) {
  const Payload payload = PayloadAfter(4096, 1000);
  auto trailer = TrailerFor(payload);
  // The archive must end where the trailer starts.
  EXPECT_FALSE(ParseTrailer(trailer, 4096 + 999 + kTrailerSize));
  // Another magic.
  auto wrong = trailer;
  wrong.back() ^= 1;
  EXPECT_FALSE(ParseTrailer(wrong, 4096 + 1000 + kTrailerSize));
  // No version.
  Payload versionless = payload;
  versionless.version.clear();
  EXPECT_FALSE(
      ParseTrailer(TrailerFor(versionless), 4096 + 1000 + kTrailerSize));
  // Too short.
  EXPECT_FALSE(ParseTrailer(base::span(trailer).first(kTrailerSize - 1),
                            4096 + 1000 + kTrailerSize));
}

TEST(PayloadTest, FindsItAtTheEnd) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  std::vector<uint8_t> bytes = ProgramStub();
  const size_t program = bytes.size();
  bytes.resize(program + 1000, 0x5A);
  const Payload payload = PayloadAfter(program, 1000);
  const auto trailer = TrailerFor(payload);
  bytes.insert(bytes.end(), trailer.begin(), trailer.end());
  base::File file = Written(temp.GetPath().AppendASCII("Pane.exe"), bytes);
  EXPECT_EQ(FindPayload(file), payload);
}

TEST(PayloadTest, FindsItBeforeASignature) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  std::vector<uint8_t> bytes = ProgramStub();
  const size_t program = bytes.size();
  bytes.resize(program + 1000, 0x5A);
  const Payload payload = PayloadAfter(program, 1000);
  const auto trailer = TrailerFor(payload);
  bytes.insert(bytes.end(), trailer.begin(), trailer.end());
  // A signature pads to 8 bytes, then appends its certificate.
  const size_t padding = (8 - bytes.size() % 8) % 8;
  bytes.resize(bytes.size() + padding, 0);
  const uint32_t certificate = static_cast<uint32_t>(bytes.size());
  bytes.resize(bytes.size() + 256, 0x77);
  std::vector<uint8_t> stub = ProgramStub(certificate, 256);
  std::copy(stub.begin(), stub.end(), bytes.begin());
  base::File file = Written(temp.GetPath().AppendASCII("Pane.exe"), bytes);
  EXPECT_EQ(FindPayload(file), payload);
}

TEST(PayloadTest, NoneInAProgramWithout) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  base::File file =
      Written(temp.GetPath().AppendASCII("pane_setup.exe"), ProgramStub());
  EXPECT_FALSE(FindPayload(file));
  // A download cut short: its trailer is gone.
  std::vector<uint8_t> bytes = ProgramStub();
  const size_t program = bytes.size();
  bytes.resize(program + 1000, 0x5A);
  const auto trailer = TrailerFor(PayloadAfter(program, 1000));
  bytes.insert(bytes.end(), trailer.begin(), trailer.end() - 20);
  base::File cut = Written(temp.GetPath().AppendASCII("Pane.exe"), bytes);
  EXPECT_FALSE(FindPayload(cut));
}

}  // namespace pane::installer
