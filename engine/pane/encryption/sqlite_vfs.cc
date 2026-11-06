// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifdef UNSAFE_BUFFERS_BUILD
// A VFS is pointer arithmetic over the buffers SQLite hands it.
#pragma allow_unsafe_buffers
#endif

#include "pane/encryption/sqlite_vfs.h"

#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/check.h"
#include "base/check_op.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/numerics/byte_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "pane/encryption/engine_keys.h"
#include "sql/initialization.h"
#include "third_party/boringssl/src/include/openssl/aead.h"
#include "third_party/boringssl/src/include/openssl/rand.h"
#include "third_party/sqlite/sqlite3.h"

namespace pane::encryption {

namespace {

constexpr char kVfsName[] = "pane";
constexpr char kParentVfsName[] = "VFSWrapper";

constexpr char kSqliteMagic[16] = "SQLite format 3";
// Pane's, in its place on page 1 of an encrypted database.
constexpr char kPaneMagic[16] = "Pane SQLite v1\n";

constexpr int kNonceSize = 12;
constexpr int kTagSize = 16;
constexpr int kInfoSize = 4;
constexpr uint8_t kFormat = 1;
static_assert(kNonceSize + kTagSize + kInfoSize == kSqliteReservedBytes);

// The clear part of page 1.
constexpr int kPageOneClearEnd = 24;
// What SQLite reads of a file as it opens it (sqlite3BtreeOpen).
constexpr int kSqliteHeaderRead = 100;

constexpr size_t kWalHeaderSize = 32;
constexpr size_t kWalFrameHeaderSize = 24;

enum class FileKind : uint8_t {
  kMain = 0,
  kJournal = 1,
  kWal = 2,
};

struct PaneFile {
  sqlite3_file base;
  // The parent's file, right after this struct.
  RAW_PTR_EXCLUSION sqlite3_file* real;
  FileKind kind;
  // The page size, once known: 0 for a new database.
  int page_size;
  // For a journal or WAL, its main file.
  RAW_PTR_EXCLUSION PaneFile* main;
  RAW_PTR_EXCLUSION EVP_AEAD_CTX* aead;
  uint8_t key_id[kInfoSize - 1];
  // A page's ciphertext on its way out; SQLite's own buffer stays as it is.
  RAW_PTR_EXCLUSION uint8_t* scratch;
  int scratch_size;
  // Its path, which SQLite keeps until the file closes.
  RAW_PTR_EXCLUSION const char* name;
};

const sqlite3_io_methods* EncryptedMethods();

bool IsValidPageSize(int size) {
  return size >= 512 && size <= 65536 && (size & (size - 1)) == 0;
}

// What each page is bound to: its kind and place, and its info bytes; on page
// 1, its clear bytes too. Never the path, since Chromium copies databases.
struct Associated {
  uint8_t bytes[1 + 8 + kInfoSize + (kPageOneClearEnd - 16)];
  size_t size;
};

Associated MakeAssociated(FileKind kind,
                          uint64_t place,
                          const uint8_t* info,
                          const uint8_t* clear) {
  Associated associated;
  associated.bytes[0] = static_cast<uint8_t>(kind);
  memcpy(associated.bytes + 1, &place, sizeof(place));
  memcpy(associated.bytes + 9, info, kInfoSize);
  associated.size = 1 + 8 + kInfoSize;

  if (clear) {
    memcpy(associated.bytes + associated.size, clear, kPageOneClearEnd - 16);
    associated.size += kPageOneClearEnd - 16;
  }

  return associated;
}

// Encrypts the page image `page` of `page_size` bytes into `out`.
void SealPage(const PaneFile* file,
              uint64_t place,
              const uint8_t* page,
              int page_size,
              uint8_t* out,
              bool is_page_one) {
  const int content_end = page_size - kSqliteReservedBytes;
  uint8_t* nonce = out + content_end;
  uint8_t* tag = nonce + kNonceSize;
  uint8_t* info = tag + kTagSize;
  RAND_bytes(nonce, kNonceSize);
  info[0] = kFormat;
  memcpy(info + 1, file->key_id, sizeof(file->key_id));

  const int start = is_page_one ? kPageOneClearEnd : 0;

  if (is_page_one) {
    memcpy(out, kPaneMagic, sizeof(kPaneMagic));
    memcpy(out + 16, page + 16, kPageOneClearEnd - 16);
  }

  const Associated associated =
      MakeAssociated(file->kind, place, info, is_page_one ? page + 16 : nullptr);
  size_t tag_size = 0;
  CHECK(EVP_AEAD_CTX_seal_scatter(file->aead, out + start, tag, &tag_size,
                                  kTagSize, nonce, kNonceSize, page + start,
                                  content_end - start, nullptr, 0,
                                  associated.bytes, associated.size));
  CHECK_EQ(tag_size, static_cast<size_t>(kTagSize));
}

// Decrypts in place. False when it does not authenticate. Its reserved bytes
// come back zero, since the WAL's checksum covers them.
bool OpenPage(const PaneFile* file,
              uint64_t place,
              uint8_t* page,
              int page_size,
              bool is_page_one) {
  const int content_end = page_size - kSqliteReservedBytes;
  const uint8_t* nonce = page + content_end;
  const uint8_t* tag = nonce + kNonceSize;
  const uint8_t* info = tag + kTagSize;
  const int start = is_page_one ? kPageOneClearEnd : 0;

  const Associated associated =
      MakeAssociated(file->kind, place, info, is_page_one ? page + 16 : nullptr);

  if (!EVP_AEAD_CTX_open_gather(file->aead, page + start, nonce, kNonceSize,
                                page + start, content_end - start, tag,
                                kTagSize, associated.bytes, associated.size)) {
    return false;
  }

  if (is_page_one) {
    memcpy(page, kSqliteMagic, sizeof(kSqliteMagic));
  }

  memset(page + content_end, 0, kSqliteReservedBytes);
  return true;
}

bool IsAllZero(const uint8_t* bytes, int size) {
  return std::all_of(bytes, bytes + size, [](uint8_t b) { return b == 0; });
}

uint8_t* ScratchOf(PaneFile* file, int size) {
  if (file->scratch_size < size) {
    sqlite3_free(file->scratch);
    file->scratch = static_cast<uint8_t*>(sqlite3_malloc(size));
    file->scratch_size = file->scratch ? size : 0;
  }

  return file->scratch;
}

// The page size a file's content is in, for a journal or WAL: the main file's,
// or, while that is still empty, what the WAL's header says.
int PageSizeOf(PaneFile* file) {
  if (file->kind == FileKind::kMain || !file->main) {
    return file->page_size;
  }

  return file->main->page_size ? file->main->page_size : file->page_size;
}

int RealRead(PaneFile* file, void* out, int amount, sqlite3_int64 offset) {
  return file->real->pMethods->xRead(file->real, out, amount, offset);
}

int RealWrite(PaneFile* file, const void* in, int amount, sqlite3_int64 offset) {
  return file->real->pMethods->xWrite(file->real, in, amount, offset);
}

// Main file ------------------------------------------------------------------

bool HasContent(const std::string& path) {
  struct stat info;
  return stat(path.c_str(), &info) == 0 && info.st_size > 0;
}

// The page size in a Pane page 1's clear bytes, read from the file.
int DiscoverPageSize(PaneFile* file) {
  uint8_t header[kPageOneClearEnd];
  const int rc = RealRead(file, header, sizeof(header), 0);

  if (rc != SQLITE_OK || memcmp(header, kPaneMagic, sizeof(kPaneMagic)) != 0) {
    return 0;
  }

  const int size = (header[16] << 8) | (header[17] << 16);
  return IsValidPageSize(size) ? size : 0;
}

// One whole page of the main file into `page`, decrypted. SQLITE_IOERR_SHORT_READ
// past the file's end, its tail zero.
int ReadMainPage(PaneFile* file, uint8_t* page, int size, uint64_t number) {
  const int rc = RealRead(file, page, size, (number - 1) * size);

  if (rc != SQLITE_OK) {
    return rc;
  }

  // A file grown ahead of its pages (SQLITE_FCNTL_CHUNK_SIZE) holds zeros.
  if (IsAllZero(page, size)) {
    return SQLITE_OK;
  }

  const bool is_page_one = number == 1;

  if (is_page_one && memcmp(page, kPaneMagic, sizeof(kPaneMagic)) != 0) {
    return SQLITE_IOERR_AUTH;
  }

  return OpenPage(file, number, page, size, is_page_one) ? SQLITE_OK
                                                         : SQLITE_IOERR_AUTH;
}

// Any read: SQLite's own are whole pages, the 100-byte header, and the 16 bytes
// at 24, and sqlite3_recover's are blocks at any offset. What a page holds is
// only ever handed out decrypted.
int ReadMain(PaneFile* file, void* out, int amount, sqlite3_int64 offset) {
  if (file->page_size == 0) {
    file->page_size = DiscoverPageSize(file);
  }

  const int size = file->page_size;

  if (size == 0) {
    // A new, still empty, database: reads come back short.
    return RealRead(file, out, amount, offset);
  }

  uint8_t* bytes = static_cast<uint8_t*>(out);
  int done = 0;

  while (done < amount) {
    const sqlite3_int64 at = offset + done;
    const uint64_t number = static_cast<uint64_t>(at / size) + 1;
    const int within = static_cast<int>(at % size);
    const int wanted = std::min(amount - done, size - within);
    int rc;

    if (within == 0 && wanted == size) {
      rc = ReadMainPage(file, bytes + done, size, number);
    } else {
      uint8_t* page = ScratchOf(file, size);

      if (!page) {
        return SQLITE_IOERR_NOMEM;
      }

      rc = ReadMainPage(file, page, size, number);

      if (rc == SQLITE_OK || rc == SQLITE_IOERR_SHORT_READ) {
        memcpy(bytes + done, page + within, wanted);
      }
    }

    if (rc == SQLITE_IOERR_SHORT_READ) {
      memset(bytes + done + (within == 0 && wanted == size ? 0 : wanted), 0,
             amount - done - (within == 0 && wanted == size ? 0 : wanted));
      return rc;
    }

    // SQLite reads the header as it opens, before any journal plays back. Of a
    // page 1 a crash tore, which the journal restores, that read comes back
    // short, which SQLite takes for an empty header, so the playback runs: an
    // error would fail the open, which Chromium's quota database answers by
    // erasing every store (patch 0010).
    if (rc == SQLITE_IOERR_AUTH && offset == 0 &&
        amount == kSqliteHeaderRead &&
        HasContent(std::string(file->name) + "-journal")) {
      memset(out, 0, amount);
      return SQLITE_IOERR_SHORT_READ;
    }

    if (rc != SQLITE_OK) {
      return rc;
    }

    done += wanted;
  }

  return SQLITE_OK;
}

int WriteMain(PaneFile* file,
              const void* in,
              int amount,
              sqlite3_int64 offset) {
  const uint8_t* page = static_cast<const uint8_t*>(in);

  if (offset == 0 && (!IsValidPageSize(amount) ||
                      memcmp(page, kSqliteMagic, sizeof(kSqliteMagic)) != 0 ||
                      page[20] != kSqliteReservedBytes)) {
    LOG(ERROR) << "A database page 1 Pane would not encrypt.";
    return SQLITE_IOERR_WRITE;
  }

  // One page size for a file's life: FileControl refuses a change.
  if (!IsValidPageSize(amount) || offset % amount != 0 ||
      (file->page_size != 0 && amount != file->page_size)) {
    return SQLITE_IOERR_WRITE;
  }

  const int size = amount;
  file->page_size = size;

  const bool is_page_one = offset == 0;

  // SQLite keeps these zero, so its checksums see the same page either way.
  if (!IsAllZero(page + size - kSqliteReservedBytes, kSqliteReservedBytes)) {
    return SQLITE_IOERR_WRITE;
  }

  uint8_t* out = ScratchOf(file, size);

  if (!out) {
    return SQLITE_IOERR_NOMEM;
  }

  SealPage(file, static_cast<uint64_t>(offset / size) + 1, page, size, out,
           is_page_one);
  return RealWrite(file, out, size, offset);
}

// Journal and WAL ------------------------------------------------------------

// An encrypted page at `offset` of a journal or WAL. During a playback, one that
// fails to authenticate comes back zero, so SQLite's own checksum ends it: a page
// a crash tore is what a rollback or a recovery stops at. Past the playback, a
// WAL's page is an error, as a main file's is: zeros would read as a malformed
// database, which Chromium razes.
enum class LoggedPageRead { kPlayback, kPage };

int ReadLoggedPage(PaneFile* file,
                   uint8_t* page,
                   int size,
                   sqlite3_int64 offset,
                   LoggedPageRead read = LoggedPageRead::kPlayback) {
  const int rc = RealRead(file, page, size, offset);

  if (rc != SQLITE_OK) {
    return rc;
  }

  if (!OpenPage(file, static_cast<uint64_t>(offset), page, size,
                /*is_page_one=*/false)) {
    if (read == LoggedPageRead::kPage) {
      return SQLITE_IOERR_AUTH;
    }

    memset(page, 0, size);
  }

  return SQLITE_OK;
}

int WriteLoggedPage(PaneFile* file,
                    const uint8_t* page,
                    int size,
                    sqlite3_int64 offset) {
  if (!IsAllZero(page + size - kSqliteReservedBytes, kSqliteReservedBytes)) {
    return SQLITE_IOERR_WRITE;
  }

  uint8_t* out = ScratchOf(file, size);

  if (!out) {
    return SQLITE_IOERR_NOMEM;
  }

  SealPage(file, static_cast<uint64_t>(offset), page, size, out,
           /*is_page_one=*/false);
  return RealWrite(file, out, size, offset);
}

// A journal's records are a page number, the page, and a checksum, after a
// header a sector long, itself often a page's size: so a page is told by its
// place, 4 bytes past a multiple of 8, where a header starts on a sector.
bool IsJournalPage(int amount, sqlite3_int64 offset) {
  return IsValidPageSize(amount) && offset % 8 == 4;
}

int ReadJournal(PaneFile* file, void* out, int amount, sqlite3_int64 offset) {
  if (IsJournalPage(amount, offset)) {
    return ReadLoggedPage(file, static_cast<uint8_t*>(out), amount, offset);
  }

  return RealRead(file, out, amount, offset);
}

int WriteJournal(PaneFile* file,
                 const void* in,
                 int amount,
                 sqlite3_int64 offset) {
  if (IsJournalPage(amount, offset)) {
    return WriteLoggedPage(file, static_cast<const uint8_t*>(in), amount,
                           offset);
  }

  return RealWrite(file, in, amount, offset);
}

// The WAL's page size, from its header, once it has one.
int DiscoverWalPageSize(PaneFile* file) {
  uint8_t header[kWalHeaderSize];

  if (RealRead(file, header, sizeof(header), 0) != SQLITE_OK) {
    return 0;
  }

  const int size = (header[8] << 24) | (header[9] << 16) | (header[10] << 8) |
                   header[11];
  return IsValidPageSize(size) ? size : 0;
}

int ReadWal(PaneFile* file, void* out, int amount, sqlite3_int64 offset) {
  if (static_cast<size_t>(amount) <= kWalHeaderSize) {
    return RealRead(file, out, amount, offset);
  }

  int size = PageSizeOf(file);

  if (size == 0) {
    file->page_size = size = DiscoverWalPageSize(file);
  }

  if (size == 0) {
    return SQLITE_IOERR_READ;
  }

  uint8_t* bytes = static_cast<uint8_t*>(out);

  if (amount == size) {
    return ReadLoggedPage(file, bytes, size, offset, LoggedPageRead::kPage);
  }

  // A frame whole, as recovery reads it: its header in the clear, then its page.
  if (static_cast<size_t>(amount) == kWalFrameHeaderSize + size) {
    const int rc = RealRead(file, bytes, kWalFrameHeaderSize, offset);

    if (rc != SQLITE_OK) {
      return rc;
    }

    return ReadLoggedPage(file, bytes + kWalFrameHeaderSize, size,
                          offset + kWalFrameHeaderSize);
  }

  return SQLITE_IOERR_READ;
}

int WriteWal(PaneFile* file,
             const void* in,
             int amount,
             sqlite3_int64 offset) {
  const uint8_t* bytes = static_cast<const uint8_t*>(in);

  if (static_cast<size_t>(amount) == kWalHeaderSize && offset == 0) {
    const int size = (bytes[8] << 24) | (bytes[9] << 16) | (bytes[10] << 8) |
                     bytes[11];
    file->page_size = IsValidPageSize(size) ? size : 0;
    return RealWrite(file, in, amount, offset);
  }

  if (static_cast<size_t>(amount) == kWalFrameHeaderSize) {
    return RealWrite(file, in, amount, offset);
  }

  const int size = PageSizeOf(file);

  if (size == 0 || amount != size) {
    return SQLITE_IOERR_WRITE;
  }

  return WriteLoggedPage(file, bytes, size, offset);
}

// The io methods -------------------------------------------------------------

PaneFile* Of(sqlite3_file* file) {
  return reinterpret_cast<PaneFile*>(file);
}

int Close(sqlite3_file* file) {
  PaneFile* pane = Of(file);
  const int rc = pane->real->pMethods ? pane->real->pMethods->xClose(pane->real)
                                      : SQLITE_OK;

  if (pane->aead) {
    EVP_AEAD_CTX_free(pane->aead);
    pane->aead = nullptr;
  }

  sqlite3_free(pane->scratch);
  pane->scratch = nullptr;
  pane->scratch_size = 0;
  pane->base.pMethods = nullptr;
  return rc;
}

int Read(sqlite3_file* file, void* out, int amount, sqlite3_int64 offset) {
  PaneFile* pane = Of(file);

  switch (pane->kind) {
    case FileKind::kMain:
      return ReadMain(pane, out, amount, offset);
    case FileKind::kJournal:
      return ReadJournal(pane, out, amount, offset);
    case FileKind::kWal:
      return ReadWal(pane, out, amount, offset);
  }
}

int Write(sqlite3_file* file,
          const void* in,
          int amount,
          sqlite3_int64 offset) {
  PaneFile* pane = Of(file);

  switch (pane->kind) {
    case FileKind::kMain:
      return WriteMain(pane, in, amount, offset);
    case FileKind::kJournal:
      return WriteJournal(pane, in, amount, offset);
    case FileKind::kWal:
      return WriteWal(pane, in, amount, offset);
  }
}

int Truncate(sqlite3_file* file, sqlite3_int64 size) {
  PaneFile* pane = Of(file);

  // An emptied database starts over, at whatever page size it next writes.
  if (size == 0 && pane->kind == FileKind::kMain) {
    pane->page_size = 0;
  }

  return pane->real->pMethods->xTruncate(pane->real, size);
}

int Sync(sqlite3_file* file, int flags) {
  sqlite3_file* real = Of(file)->real;
  return real->pMethods->xSync(real, flags);
}

int FileSize(sqlite3_file* file, sqlite3_int64* size) {
  sqlite3_file* real = Of(file)->real;
  return real->pMethods->xFileSize(real, size);
}

int Lock(sqlite3_file* file, int level) {
  sqlite3_file* real = Of(file)->real;
  return real->pMethods->xLock(real, level);
}

int Unlock(sqlite3_file* file, int level) {
  sqlite3_file* real = Of(file)->real;
  return real->pMethods->xUnlock(real, level);
}

int CheckReservedLock(sqlite3_file* file, int* reserved) {
  sqlite3_file* real = Of(file)->real;
  return real->pMethods->xCheckReservedLock(real, reserved);
}

int FileControl(sqlite3_file* file, int op, void* argument) {
  PaneFile* pane = Of(file);

  // A database keeps its page size: SQLite would read the file at a size it is
  // not in, and journal what it read so. PRAGMA page_size=N, then VACUUM, does
  // nothing here. A new database's still takes.
  if (op == SQLITE_FCNTL_PRAGMA && pane->kind == FileKind::kMain &&
      pane->page_size != 0) {
    char** pragma = static_cast<char**>(argument);

    if (pragma[2] && sqlite3_stricmp(pragma[1], "page_size") == 0) {
      return SQLITE_OK;
    }
  }

  return pane->real->pMethods->xFileControl(pane->real, op, argument);
}

int SectorSize(sqlite3_file* file) {
  sqlite3_file* real = Of(file)->real;
  return real->pMethods->xSectorSize(real);
}

// Never a partial read of a page (pager.c), and writes stay power-safe, so the
// WAL never splits a frame to pad a sector.
int DeviceCharacteristics(sqlite3_file* file) {
  sqlite3_file* real = Of(file)->real;
  return real->pMethods->xDeviceCharacteristics(real) &
         ~SQLITE_IOCAP_SUBPAGE_READ;
}

int ShmMap(sqlite3_file* file,
           int region,
           int size,
           int extend,
           void volatile** mapped) {
  sqlite3_file* real = Of(file)->real;
  return real->pMethods->xShmMap(real, region, size, extend, mapped);
}

int ShmLock(sqlite3_file* file, int offset, int count, int flags) {
  sqlite3_file* real = Of(file)->real;
  return real->pMethods->xShmLock(real, offset, count, flags);
}

void ShmBarrier(sqlite3_file* file) {
  sqlite3_file* real = Of(file)->real;
  real->pMethods->xShmBarrier(real);
}

int ShmUnmap(sqlite3_file* file, int remove) {
  sqlite3_file* real = Of(file)->real;
  return real->pMethods->xShmUnmap(real, remove);
}

// Version 2: with no xFetch, SQLite never maps a file (pager.c), which would
// hand it ciphertext.
constexpr sqlite3_io_methods kMethods = {
    .iVersion = 2,
    .xClose = &Close,
    .xRead = &Read,
    .xWrite = &Write,
    .xTruncate = &Truncate,
    .xSync = &Sync,
    .xFileSize = &FileSize,
    .xLock = &Lock,
    .xUnlock = &Unlock,
    .xCheckReservedLock = &CheckReservedLock,
    .xFileControl = &FileControl,
    .xSectorSize = &SectorSize,
    .xDeviceCharacteristics = &DeviceCharacteristics,
    .xShmMap = &ShmMap,
    .xShmLock = &ShmLock,
    .xShmBarrier = &ShmBarrier,
    .xShmUnmap = &ShmUnmap,
};

const sqlite3_io_methods* EncryptedMethods() {
  return &kMethods;
}

// The VFS --------------------------------------------------------------------

sqlite3_vfs* ParentOf(sqlite3_vfs* vfs) {
  return static_cast<sqlite3_vfs*>(vfs->pAppData);
}

// What a main file's page 1 says at its open: an empty file is a new database;
// a plain one in a protected path, or one under another key, stops the process,
// since an error would reach Chromium's corruption handling. A page 1 a crash
// tore passes only while a journal or WAL is there to restore it.
void CheckMainAtOpen(PaneFile* file, const char* name) {
  sqlite3_int64 file_size = 0;

  if (FileSize(&file->base, &file_size) != SQLITE_OK || file_size == 0) {
    return;
  }

  uint8_t header[kPageOneClearEnd];

  if (RealRead(file, header, sizeof(header), 0) != SQLITE_OK) {
    return;
  }

  CHECK(memcmp(header, kSqliteMagic, sizeof(kSqliteMagic)) != 0)
      << "A plain database in a profile, not migrated.";

  if (memcmp(header, kPaneMagic, sizeof(kPaneMagic)) != 0) {
    return;
  }

  file->page_size = DiscoverPageSize(file);

  if (file->page_size == 0) {
    return;
  }

  uint8_t info[kInfoSize];

  if (RealRead(file, info, kInfoSize, file->page_size - kInfoSize) !=
      SQLITE_OK) {
    return;
  }

  const bool same_key =
      info[0] == kFormat &&
      memcmp(info + 1, file->key_id, sizeof(file->key_id)) == 0;

  if (!same_key) {
    const std::string main(name);
    CHECK(HasContent(main + "-journal") || HasContent(main + "-wal"))
        << "A database under another key.";
  }
}

int Open(sqlite3_vfs* vfs,
         const char* name,
         sqlite3_file* file,
         int flags,
         int* out_flags) {
  sqlite3_vfs* parent = ParentOf(vfs);
  PaneFile* pane = Of(file);
  pane->base.pMethods = nullptr;

  const int kinds =
      flags & (SQLITE_OPEN_MAIN_DB | SQLITE_OPEN_MAIN_JOURNAL | SQLITE_OPEN_WAL);

  std::optional<PathVerdict> verdict;

  if (name && kinds) {
    verdict = Classify(base::FilePath(name));
  }

  if (!verdict || verdict->kind != PathKind::kEncrypted) {
    return parent->xOpen(parent, name, file, flags, out_flags);
  }

  memset(pane, 0, sizeof(PaneFile));
  pane->real = reinterpret_cast<sqlite3_file*>(pane + 1);
  pane->name = name;
  pane->kind = (flags & SQLITE_OPEN_MAIN_DB)       ? FileKind::kMain
               : (flags & SQLITE_OPEN_MAIN_JOURNAL) ? FileKind::kJournal
                                                    : FileKind::kWal;

  if (pane->kind != FileKind::kMain) {
    // Same folder, so same verdict: its main file is one of these, even with
    // sqlite3_recover's methods wrapped around it.
    sqlite3_file* main = sqlite3_database_file_object(name);
    CHECK(main) << "A journal or WAL in a profile with no main file.";
    pane->main = Of(main);
  }

  const int rc = parent->xOpen(parent, name, pane->real, flags, out_flags);

  if (rc != SQLITE_OK) {
    return rc;
  }

  pane->aead = EVP_AEAD_CTX_new(EVP_aead_aes_256_gcm(),
                                verdict->keys->sqlite_pages.data(),
                                verdict->keys->sqlite_pages.size(), kTagSize);
  CHECK(pane->aead);

  const Key key_id = DeriveKey(verdict->keys->sqlite_pages,
                               base::span<const uint8_t>(), "Pane SQLite key id");
  memcpy(pane->key_id, key_id.data(), sizeof(pane->key_id));
  pane->base.pMethods = EncryptedMethods();

  if (pane->kind == FileKind::kMain) {
    CheckMainAtOpen(pane, name);
  }

  return SQLITE_OK;
}

int Delete(sqlite3_vfs* vfs, const char* name, int sync_directory) {
  sqlite3_vfs* parent = ParentOf(vfs);
  return parent->xDelete(parent, name, sync_directory);
}

int Access(sqlite3_vfs* vfs, const char* name, int flags, int* result) {
  sqlite3_vfs* parent = ParentOf(vfs);
  return parent->xAccess(parent, name, flags, result);
}

int FullPathname(sqlite3_vfs* vfs, const char* name, int size, char* out) {
  sqlite3_vfs* parent = ParentOf(vfs);
  return parent->xFullPathname(parent, name, size, out);
}

void* DlOpen(sqlite3_vfs* vfs, const char* name) {
  sqlite3_vfs* parent = ParentOf(vfs);
  return parent->xDlOpen(parent, name);
}

void DlError(sqlite3_vfs* vfs, int size, char* out) {
  sqlite3_vfs* parent = ParentOf(vfs);
  parent->xDlError(parent, size, out);
}

void (*DlSym(sqlite3_vfs* vfs, void* handle, const char* symbol))(void) {
  sqlite3_vfs* parent = ParentOf(vfs);
  return parent->xDlSym(parent, handle, symbol);
}

void DlClose(sqlite3_vfs* vfs, void* handle) {
  sqlite3_vfs* parent = ParentOf(vfs);
  parent->xDlClose(parent, handle);
}

int Randomness(sqlite3_vfs* vfs, int size, char* out) {
  sqlite3_vfs* parent = ParentOf(vfs);
  return parent->xRandomness(parent, size, out);
}

int Sleep(sqlite3_vfs* vfs, int microseconds) {
  sqlite3_vfs* parent = ParentOf(vfs);
  return parent->xSleep(parent, microseconds);
}

int CurrentTime(sqlite3_vfs* vfs, double* now) {
  sqlite3_vfs* parent = ParentOf(vfs);
  return parent->xCurrentTime(parent, now);
}

int GetLastError(sqlite3_vfs* vfs, int size, char* out) {
  sqlite3_vfs* parent = ParentOf(vfs);
  return parent->xGetLastError(parent, size, out);
}

int CurrentTimeInt64(sqlite3_vfs* vfs, sqlite3_int64* now) {
  sqlite3_vfs* parent = ParentOf(vfs);
  return parent->xCurrentTimeInt64(parent, now);
}

// Every connection Pane's files can reach: a new encrypted database, and the
// temporary and in-memory ones Raze and sql::Recovery back up from, whose page 1
// a backup copies verbatim.
int ReserveBytes(sqlite3* db, const char** error, const void*) {
  const char* name = sqlite3_db_filename(db, "main");
  sqlite3_file* file = nullptr;
  sqlite3_file_control(db, "main", SQLITE_FCNTL_FILE_POINTER, &file);

  const bool in_memory = !name || name[0] == '\0';

  if (in_memory || (file && file->pMethods == EncryptedMethods())) {
    int reserved = kSqliteReservedBytes;
    sqlite3_file_control(db, "main", SQLITE_FCNTL_RESERVE_BYTES, &reserved);
  }

  return SQLITE_OK;
}

void InstallOnce() {
  sql::EnsureSqliteInitialized();
  sqlite3_vfs* parent = sqlite3_vfs_find(kParentVfsName);
  CHECK(parent);

  static sqlite3_vfs vfs = {
      .iVersion = 3,
      .szOsFile = static_cast<int>(sizeof(PaneFile)) + parent->szOsFile,
      .mxPathname = parent->mxPathname,
      .zName = kVfsName,
      .pAppData = parent,
      .xOpen = &Open,
      .xDelete = &Delete,
      .xAccess = &Access,
      .xFullPathname = &FullPathname,
      .xDlOpen = &DlOpen,
      .xDlError = &DlError,
      .xDlSym = &DlSym,
      .xDlClose = &DlClose,
      .xRandomness = &Randomness,
      .xSleep = &Sleep,
      .xCurrentTime = &CurrentTime,
      .xGetLastError = &GetLastError,
      .xCurrentTimeInt64 = &CurrentTimeInt64,
  };

  CHECK_EQ(sqlite3_vfs_register(&vfs, /*makeDflt=*/1), SQLITE_OK);
  CHECK_EQ(sqlite3_auto_extension(
               reinterpret_cast<void (*)(void)>(&ReserveBytes)),
           SQLITE_OK);
}

std::optional<std::array<char, 16>> MagicOf(const base::FilePath& path) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);

  if (!file.IsValid()) {
    return std::nullopt;
  }

  std::array<char, 16> magic;

  if (file.Read(0, base::as_writable_byte_span(magic)) != 16u) {
    return std::nullopt;
  }

  return magic;
}

// The temporary copy's name, in the database's folder, in a URI: a path with a
// space or a '?' in it would otherwise not parse.
std::string UriOf(const base::FilePath& path) {
  std::string uri = "file:";

  for (const char c : path.value()) {
    if (base::IsAsciiAlpha(c) || base::IsAsciiDigit(c) ||
        std::string_view("/-_.~").find(c) != std::string_view::npos) {
      uri.push_back(c);
    } else {
      base::StringAppendF(&uri, "%%%02X", static_cast<unsigned char>(c));
    }
  }

  return uri;
}

}  // namespace

void InstallSqliteVfs() {
  static const bool installed = [] {
    InstallOnce();
    return true;
  }();
  std::ignore = installed;
}

bool IsPlainSqliteDatabase(const base::FilePath& path) {
  std::optional<std::array<char, 16>> magic = MagicOf(path);
  return magic && memcmp(magic->data(), kSqliteMagic, 16) == 0;
}

bool IsEncryptedSqliteDatabase(const base::FilePath& path) {
  std::optional<std::array<char, 16>> magic = MagicOf(path);
  return magic && memcmp(magic->data(), kPaneMagic, 16) == 0;
}

bool SqliteQuickCheckPasses(const base::FilePath& path, const char* vfs) {
  sqlite3* db = nullptr;
  bool passes = false;

  if (sqlite3_open_v2(path.value().c_str(), &db, SQLITE_OPEN_READONLY, vfs) ==
      SQLITE_OK) {
    const int checked = sqlite3_exec(
        db, "PRAGMA quick_check(1)",
        [](void* said_ok, int columns, char** values, char**) {
          *static_cast<bool*>(said_ok) =
              columns == 1 && values[0] && strcmp(values[0], "ok") == 0;
          return 0;
        },
        &passes, nullptr);
    passes = passes && checked == SQLITE_OK;
  }

  sqlite3_close(db);
  return passes;
}

SqliteMigration MigrateSqliteDatabase(const base::FilePath& path) {
  InstallSqliteVfs();

  if (!IsPlainSqliteDatabase(path)) {
    return SqliteMigration::kNothingToDo;
  }

  CHECK(Classify(path).kind == PathKind::kEncrypted);

  base::FilePath temporary = path.AddExtension(".pane-migrating");
  base::DeleteFile(temporary);
  base::DeleteFile(temporary.InsertBeforeExtension("-journal"));

  sqlite3* raw = nullptr;
  const std::string name = path.value();

  // On Chromium's own VFS: the open settles a journal or WAL left in the clear.
  if (sqlite3_open_v2(name.c_str(), &raw,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_URI,
                      kParentVfsName) != SQLITE_OK) {
    sqlite3_close(raw);
    return SqliteMigration::kFailed;
  }

  int reserved = kSqliteReservedBytes;
  sqlite3_file_control(raw, "main", SQLITE_FCNTL_RESERVE_BYTES, &reserved);

  const std::string vacuum =
      base::StrCat({"VACUUM main INTO '", UriOf(temporary), "?vfs=", kVfsName,
                    "'"});
  const int vacuumed = sqlite3_exec(raw, vacuum.c_str(), nullptr, nullptr, nullptr);
  sqlite3_close(raw);

  if (vacuumed != SQLITE_OK) {
    base::DeleteFile(temporary);
    const int primary = vacuumed & 0xff;
    return primary == SQLITE_CORRUPT || primary == SQLITE_NOTADB
               ? SqliteMigration::kCorrupt
               : SqliteMigration::kFailed;
  }

  // The copy decrypts, whole, before it replaces anything. One that fails the
  // check with an original that passes it is this code's failure; with one that
  // fails it too, the copy is as good as what it copies.
  const bool verified = SqliteQuickCheckPasses(temporary, kVfsName) ||
                        (IsEncryptedSqliteDatabase(temporary) &&
                         !SqliteQuickCheckPasses(path, kParentVfsName));

  if (!verified || !IsEncryptedSqliteDatabase(temporary)) {
    base::DeleteFile(temporary);
    return SqliteMigration::kFailed;
  }

  {
    base::File file(temporary, base::File::FLAG_OPEN | base::File::FLAG_WRITE);

    if (!file.IsValid() || !file.Flush()) {
      base::DeleteFile(temporary);
      return SqliteMigration::kFailed;
    }
  }

  if (!base::ReplaceFile(temporary, path, nullptr)) {
    base::DeleteFile(temporary);
    return SqliteMigration::kFailed;
  }

  base::DeleteFile(base::FilePath(name + "-journal"));
  base::DeleteFile(base::FilePath(name + "-wal"));
  base::DeleteFile(base::FilePath(name + "-shm"));

  base::File directory(path.DirName(),
                       base::File::FLAG_OPEN | base::File::FLAG_READ);

  if (directory.IsValid()) {
    directory.Flush();
  }

  return SqliteMigration::kMigrated;
}

}  // namespace pane::encryption
