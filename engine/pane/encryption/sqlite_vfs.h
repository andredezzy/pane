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

}  // namespace pane::encryption

#endif  // PANE_ENCRYPTION_SQLITE_VFS_H_
