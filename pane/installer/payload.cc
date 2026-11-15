// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/payload.h"

#include <algorithm>
#include <string_view>
#include <vector>

#include "base/containers/buffer_iterator.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/numerics/byte_conversions.h"
#include "base/strings/string_util.h"

namespace pane::installer {

namespace {

// The longest version the trailer holds, with its NUL.
constexpr size_t kVersionBytes = 32;

// What a signature pads before the certificate it appends: at most 7 zeros,
// so the certificate starts on 8 bytes (Authenticode).
constexpr uint64_t kMostPadding = 7;

// The PE header's fields that name the certificate (winnt.h's): e_lfanew, the
// optional header after the signature and the file header, its magic for a
// 64-bit image, and its security directory (the fifth of its directories).
constexpr size_t kNewHeaderOffset = 0x3C;
constexpr size_t kOptionalHeaderOffset = 4 + 20;
constexpr uint16_t kPe32PlusMagic = 0x20B;
constexpr size_t kSecurityDirectoryOffset = 112 + 4 * 8;

// `count` bytes at `offset`, or nothing where the file is shorter.
std::optional<std::vector<uint8_t>> ReadAt(base::File& file,
                                           uint64_t offset,
                                           size_t count) {
  std::vector<uint8_t> bytes(count);
  if (!file.ReadAndCheck(static_cast<int64_t>(offset), bytes)) {
    return std::nullopt;
  }
  return bytes;
}

// Where the certificate a signature appended starts: the security
// directory's file offset, where it names one that runs to the file's end.
std::optional<uint64_t> CertificateStart(base::File& file, uint64_t length) {
  const std::optional<std::vector<uint8_t>> dos =
      ReadAt(file, 0, kNewHeaderOffset + 4);
  if (!dos || (*dos)[0] != 'M' || (*dos)[1] != 'Z') {
    return std::nullopt;
  }
  const uint64_t pe = base::U32FromLittleEndian(
      base::span(*dos).subspan<kNewHeaderOffset, 4u>());
  const std::optional<std::vector<uint8_t>> header =
      ReadAt(file, pe, kOptionalHeaderOffset + kSecurityDirectoryOffset + 8);
  if (!header || base::as_string_view(base::span(*header).first<4u>()) !=
                     std::string_view("PE\0\0", 4)) {
    return std::nullopt;
  }
  const base::span<const uint8_t> optional =
      base::span(*header).subspan(kOptionalHeaderOffset);
  if (base::U16FromLittleEndian(optional.first<2u>()) != kPe32PlusMagic) {
    return std::nullopt;
  }
  const uint64_t start = base::U32FromLittleEndian(
      optional.subspan(kSecurityDirectoryOffset).first<4u>());
  const uint64_t size = base::U32FromLittleEndian(
      optional.subspan(kSecurityDirectoryOffset + 4).first<4u>());
  if (start == 0 || size == 0 || start + size != length) {
    return std::nullopt;
  }
  return start;
}

}  // namespace

std::optional<Payload> ParseTrailer(base::span<const uint8_t> trailer,
                                    uint64_t end) {
  if (trailer.size() != kTrailerSize || end < kTrailerSize) {
    return std::nullopt;
  }
  base::BufferIterator<const uint8_t> reader(trailer);
  Payload payload;
  payload.offset = base::U64FromLittleEndian(*reader.Span<uint8_t, 8>());
  payload.size = base::U64FromLittleEndian(*reader.Span<uint8_t, 8>());
  payload.unpacked_size = base::U64FromLittleEndian(*reader.Span<uint8_t, 8>());
  base::span(payload.sha256).copy_from(*reader.Span<uint8_t, 32>());
  const base::span<const uint8_t, kVersionBytes> version =
      *reader.Span<uint8_t, kVersionBytes>();
  if (base::as_string_view(*reader.Span<uint8_t, 16>()) !=
      std::string_view(kTrailerMagic, 16)) {
    return std::nullopt;
  }
  // The version: printable ASCII, then NULs.
  const auto nul = std::ranges::find(version, 0);
  if (nul == version.begin() || nul == version.end() ||
      !std::all_of(nul, version.end(),
                   [](uint8_t byte) { return byte == 0; }) ||
      !std::all_of(version.begin(), nul, [](uint8_t byte) {
        return base::IsAsciiPrintable(static_cast<char>(byte));
      })) {
    return std::nullopt;
  }
  payload.version.assign(version.begin(), nul);
  // The archive ends where the trailer starts.
  if (payload.size == 0 || payload.offset > end - kTrailerSize ||
      end - kTrailerSize - payload.offset != payload.size) {
    return std::nullopt;
  }
  return payload;
}

std::array<uint8_t, kTrailerSize> TrailerFor(const Payload& payload) {
  std::array<uint8_t, kTrailerSize> trailer = {};
  base::span<uint8_t> out(trailer);
  out.subspan<0u, 8u>().copy_from(base::U64ToLittleEndian(payload.offset));
  out.subspan<8u, 8u>().copy_from(base::U64ToLittleEndian(payload.size));
  out.subspan<16u, 8u>().copy_from(
      base::U64ToLittleEndian(payload.unpacked_size));
  out.subspan<24u, 32u>().copy_from(payload.sha256);
  const size_t length = std::min(payload.version.size(), kVersionBytes - 1);
  out.subspan<56u>().first(length).copy_from(
      base::as_byte_span(payload.version).first(length));
  out.subspan<88u, 16u>().copy_from(
      base::as_byte_span(std::string_view(kTrailerMagic, 16)));
  return trailer;
}

std::optional<Payload> FindPayload(base::File& file) {
  const int64_t length = file.GetLength();
  if (length < static_cast<int64_t>(kTrailerSize)) {
    return std::nullopt;
  }
  uint64_t end = static_cast<uint64_t>(length);
  uint64_t padding = 0;
  if (const std::optional<uint64_t> certificate = CertificateStart(file, end)) {
    end = *certificate;
    padding = kMostPadding;
  }
  // The trailer ends at `end`, or, before a certificate, up to its padding's
  // zeros earlier.
  for (uint64_t skipped = 0;
       skipped <= padding && end - skipped >= kTrailerSize; ++skipped) {
    const uint64_t at = end - skipped;
    if (skipped > 0) {
      const std::optional<std::vector<uint8_t>> zero = ReadAt(file, at, 1);
      if (!zero || (*zero)[0] != 0) {
        return std::nullopt;
      }
    }
    const std::optional<std::vector<uint8_t>> trailer =
        ReadAt(file, at - kTrailerSize, kTrailerSize);
    if (!trailer) {
      return std::nullopt;
    }
    if (std::optional<Payload> payload = ParseTrailer(*trailer, at)) {
      return payload;
    }
  }
  return std::nullopt;
}

}  // namespace pane::installer
