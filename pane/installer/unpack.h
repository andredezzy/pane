// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_UNPACK_H_
#define PANE_INSTALLER_UNPACK_H_

#include <windows.h>

#include <stdint.h>

#include <atomic>

#include "base/files/file_path.h"
#include "pane/installer/payload.h"

namespace pane::installer {

// How far an unpack has gone, which the window reads at each frame: the
// archive's bytes the reader has read, and the files' bytes on the disk.
struct UnpackProgress {
  std::atomic<uint64_t> read{0};
  std::atomic<uint64_t> written{0};
};

struct UnpackResult {
  enum class Outcome {
    kDone,
    kCancelled,
    // Windows or an antivirus refused a file: `file` names it.
    kBlocked,
    // The disk, the archive, or anything else: `error` says what.
    kFailed,
  };
  Outcome outcome = Outcome::kFailed;
  // The file being written when it stopped, as the archive names it.
  base::FilePath file;
  // The system's error, for the code a failure shows.
  DWORD error = 0;
};

// Unpacks `payload`, inside the installer at `installer`, into `folder`,
// which exists and is empty, through the reader and the writer Chromium's
// installer::UnPackArchiveBuffer runs (seven_zip::SevenZipReader over the
// archive mapped in memory, installer::UnbufferedFileWriter for each file),
// with a delegate of Pane's in place of lzma_util.cc's, the same steps for
// each file, which counts what it reads and writes and stops at the next file
// once `cancel` is set: UnPackArchive tells nothing as it goes and can't be
// stopped. Blocks.
UnpackResult Unpack(const base::FilePath& installer,
                    const Payload& payload,
                    const base::FilePath& folder,
                    UnpackProgress& progress,
                    const std::atomic<bool>& cancel);

// Whether the payload's bytes in `installer` hash to its SHA-256, read
// apart from the unpack, through Windows' own SHA-256 (CNG). Blocks.
bool PayloadIsIntact(const base::FilePath& installer, const Payload& payload);

}  // namespace pane::installer

#endif  // PANE_INSTALLER_UNPACK_H_
