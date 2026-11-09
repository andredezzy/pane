// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_FILE_OPERATIONS_H_
#define PANE_MODEL_MAC_FILE_OPERATIONS_H_

#include <stdint.h>

#include <string>

#include "base/files/file_path.h"
#include "base/functional/function_ref.h"
#include "base/types/expected.h"

namespace pane::model {

// What the Swift ported to pane/model/mac asked of Foundation's FileManager
// and URL, with their rules where //base's file_util has others: the volume,
// the wipe, and the moves of an earlier Pane's data run on these. An error is
// the item's path and why, for the log.

// "<path>: <why>", why being errno's text for the call that just failed.
std::string LastFileError(const base::FilePath& path);

// Whether anything is at `path`, a link included, wherever it leads, as
// FileManager's attributesOfItem reads the link itself.
bool ItemExists(const base::FilePath& path);

// A folder, not a link to one.
bool IsPlainFolder(const base::FilePath& path);

// FileManager.copyItem: a file with its attributes, cloned where the volume
// can; a link as a link; a folder with everything under it, failing on one it
// can't read. Fails when something is at `to` already. Folders are made
// 0700, as //base makes them, where Foundation kept their modes.
base::expected<void, std::string> CopyItem(const base::FilePath& from,
                                           const base::FilePath& to);

// Everything under `from` into `to`, which is made if missing, file by file,
// as a FileManager enumerator walks it: links as links, folders made as they
// come, and `copied` told each file's size once it is copied. A folder it
// can't read fails it, where FileManager's enumerator passed over one.
base::expected<void, std::string> CopyContents(
    const base::FilePath& from,
    const base::FilePath& to,
    base::FunctionRef<void(int64_t size)> copied);

// FileManager.moveItem: a rename, or across volumes a copy and then a
// removal. Fails when something is at `to` already, where rename(2) would
// replace it.
base::expected<void, std::string> MoveItem(const base::FilePath& from,
                                           const base::FilePath& to);

// FileManager.removeItem: the item and everything under it; a link, never
// what it leads to. Fails when nothing is there.
base::expected<void, std::string> RemoveItem(const base::FilePath& path);

// FileManager.createFile(atPath:contents: nil): an empty file, over what was
// there.
bool Touch(const base::FilePath& path);

// URL.standardizedFileURL's path, for an absolute one: "." and ".." resolved
// as written, empty components and a trailing separator dropped, and an
// initial /private/var/automount, /var/automount, or /private removed where
// what is left exists, as Foundation does (/var is /private/var). A relative
// path comes back as it is.
base::FilePath StandardizedPath(const base::FilePath& path);

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_FILE_OPERATIONS_H_
