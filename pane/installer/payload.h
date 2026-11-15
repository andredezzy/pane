// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_PAYLOAD_H_
#define PANE_INSTALLER_PAYLOAD_H_

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <optional>
#include <string>

#include "base/containers/span.h"

namespace base {
class File;
}

namespace pane::installer {

// What scripts/package-win.sh appends to Pane's installer, which is the same
// program for every version (ADR 0127): the app folder's payload, the 7z
// archive package-win.sh makes of it, then this trailer. They sit at the
// file's end, or just before the Authenticode certificate a signature adds
// after them (U6), so a signed installer still finds its payload.
struct Payload {
  friend bool operator==(const Payload&, const Payload&) = default;

  // Where the archive starts in the installer, and its length: it ends where
  // the trailer begins.
  uint64_t offset = 0;
  uint64_t size = 0;
  // The app folder's size once unpacked, for the space it takes.
  uint64_t unpacked_size = 0;
  // The archive's SHA-256, checked as it is unpacked.
  std::array<uint8_t, 32> sha256 = {};
  // Pane's version, packaging/VERSION's: the one the payload's pane-version
  // holds.
  std::string version;
};

// The trailer: the archive's offset, its size, and the folder's unpacked
// size, each 8 bytes little-endian; the archive's SHA-256; the version in 32
// bytes, NUL-padded; then kTrailerMagic.
inline constexpr size_t kTrailerSize = 8 + 8 + 8 + 32 + 32 + 16;
inline constexpr char kTrailerMagic[] = "PanePayload/v1.0";

// The trailer in `trailer`, the kTrailerSize bytes that end at `end` in the
// file: nullopt where they are no trailer, or name an archive that doesn't
// end where the trailer starts.
std::optional<Payload> ParseTrailer(base::span<const uint8_t> trailer,
                                    uint64_t end);

// The trailer `payload` is written as, for the tests (package-win.sh writes
// the same bytes).
std::array<uint8_t, kTrailerSize> TrailerFor(const Payload& payload);

// The payload of the installer open in `file`: its trailer at the file's end,
// or before the certificate its PE header's security directory names, with
// the zeros a signature pads before it. nullopt where there is none: a
// program built without one, or a download cut short.
std::optional<Payload> FindPayload(base::File& file);

}  // namespace pane::installer

#endif  // PANE_INSTALLER_PAYLOAD_H_
