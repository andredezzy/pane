// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_ENCRYPTION_SQLITE_VFS_H_
#define PANE_ENCRYPTION_SQLITE_VFS_H_

#include "base/files/file_path.h"

namespace pane::encryption {

// Registers the "pane" SQLite VFS as the default, over Chromium's VFSWrapper,
// and the extension that reserves its 32 bytes in every connection. It
// encrypts the pages of the databases in a profile's folder, and opens
// everything else straight on its parent. Safe to call again, and from any
// thread, before any of a profile's databases opens.
void InstallSqliteVfs();

// Page format, per page: the page's content, its last 32 bytes reserved for a
// 12-byte random nonce, a 16-byte AES-256-GCM tag, and 4 bytes of format and
// key id. On page 1, bytes 0-15 carry Pane's magic in place of SQLite's, and
// bytes 16-23 stay clear, authenticated.
inline constexpr int kSqliteReservedBytes = 32;

enum class SqliteMigration {
  // A plain database re-encrypted, and in its place.
  kMigrated,
  // Nothing to do: already encrypted, empty, or not a database.
  kNothingToDo,
  // Its copy failed, or would not verify: the original stays, as it was.
  kFailed,
  // SQLite finds it malformed, as the engine would when it read that page and
  // razed it: the original stays, for the caller to drop.
  kCorrupt,
};

// What a conversion writes beside a file before it takes the file's place
// (MigrateSqliteDatabase, and a profile's other files): a kill leaves it, and
// the next conversion deletes it.
inline constexpr base::FilePath::StringViewType kMigratingSuffix =
    FILE_PATH_LITERAL(".pane-migrating");

// Encrypts a plain database of a closed profile in place, surviving a kill at
// any point: the raw open settles any journal left in the clear, VACUUM INTO
// writes a temporary encrypted copy, which is verified and fsynced, then
// renamed over the original. The profile's key must be registered.
SqliteMigration MigrateSqliteDatabase(const base::FilePath& path);

// Whether `PRAGMA quick_check` says "ok" of the database open in `path`: it
// reports what it finds as rows, and returns SQLITE_OK whatever they say.
bool SqliteQuickCheckPasses(const base::FilePath& path, const char* vfs);

// Whether `path` starts with SQLite's own magic.
bool IsPlainSqliteDatabase(const base::FilePath& path);

// Whether `path` starts with Pane's page format.
bool IsEncryptedSqliteDatabase(const base::FilePath& path);

}  // namespace pane::encryption

#endif  // PANE_ENCRYPTION_SQLITE_VFS_H_
