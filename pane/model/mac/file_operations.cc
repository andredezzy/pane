// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/file_operations.h"

#include <copyfile.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include <array>
#include <string_view>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/types/expected_macros.h"

namespace pane::model {

namespace {

base::unexpected<std::string> Failed(const base::FilePath& path) {
  return base::unexpected(LastFileError(path));
}

base::expected<void, std::string> CopyLink(const base::FilePath& from,
                                           const base::FilePath& to) {
  base::FilePath target;
  if (!base::ReadSymbolicLink(from, &target)) {
    return Failed(from);
  }
  if (!base::CreateSymbolicLink(target, to)) {
    return Failed(to);
  }
  return base::ok();
}

// With its attributes (mode, times, ACL, extended attributes), and a clone
// where both are on one APFS volume, as FileManager copies.
base::expected<void, std::string> CloneFile(const base::FilePath& from,
                                            const base::FilePath& to) {
  if (copyfile(from.value().c_str(), to.value().c_str(), nullptr,
               COPYFILE_CLONE) != 0) {
    return Failed(to);
  }
  return base::ok();
}

// Everything under `from` into `to`, made if missing, file by file: links as
// links, folders made as they come, and `copied` told each file's size.
base::expected<void, std::string> CopyTree(
    const base::FilePath& from,
    const base::FilePath& to,
    base::FunctionRef<void(int64_t size)> copied,
    base::FileEnumerator::ErrorPolicy error_policy) {
  if (!base::CreateDirectory(to)) {
    return Failed(to);
  }
  // Each path relative to `from`, as given, so a link on the way to it (/var
  // to /private/var) never shifts it.
  base::FileEnumerator entries(
      from, /*recursive=*/true,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
          base::FileEnumerator::SHOW_SYM_LINKS,
      base::FilePath::StringType(),
      base::FileEnumerator::FolderSearchPolicy::MATCH_ONLY, error_policy);
  for (base::FilePath entry = entries.Next(); !entry.empty();
       entry = entries.Next()) {
    base::FilePath target = to;
    if (!from.AppendRelativePath(entry, &target)) {
      return base::unexpected(
          base::StrCat({entry.value(), ": outside ", from.value()}));
    }
    const base::stat_wrapper_t& info = entries.GetInfo().stat();
    if (S_ISLNK(info.st_mode)) {
      RETURN_IF_ERROR(CopyLink(entry, target));
    } else if (S_ISDIR(info.st_mode)) {
      if (!base::CreateDirectory(target)) {
        return Failed(target);
      }
    } else {
      RETURN_IF_ERROR(CloneFile(entry, target));
      copied(info.st_size);
    }
  }
  if (entries.GetError() != base::File::FILE_OK) {
    return base::unexpected(base::StrCat(
        {from.value(), ": ", base::File::ErrorToString(entries.GetError())}));
  }
  return base::ok();
}

}  // namespace

std::string LastFileError(const base::FilePath& path) {
  return base::StrCat(
      {path.value(), ": ",
       logging::SystemErrorCodeToString(logging::GetLastSystemErrorCode())});
}

bool ItemExists(const base::FilePath& path) {
  base::stat_wrapper_t info;
  return base::File::Lstat(path, &info) == 0;
}

bool IsPlainFolder(const base::FilePath& path) {
  base::stat_wrapper_t info;
  return base::File::Lstat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

base::expected<void, std::string> CopyItem(const base::FilePath& from,
                                           const base::FilePath& to) {
  base::stat_wrapper_t info;
  if (base::File::Lstat(from, &info) != 0) {
    return Failed(from);
  }
  if (ItemExists(to)) {
    return base::unexpected(base::StrCat({to.value(), ": already exists"}));
  }
  if (S_ISLNK(info.st_mode)) {
    return CopyLink(from, to);
  }
  if (S_ISDIR(info.st_mode)) {
    // A folder it can't read fails the copy, as FileManager's does.
    return CopyTree(
        from, to, [](int64_t) {},
        base::FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  }
  return CloneFile(from, to);
}

base::expected<void, std::string> CopyContents(
    const base::FilePath& from,
    const base::FilePath& to,
    base::FunctionRef<void(int64_t size)> copied) {
  // A folder it can't read is passed over, as FileManager's enumerator does.
  return CopyTree(from, to, copied,
                  base::FileEnumerator::ErrorPolicy::IGNORE_ERRORS);
}

base::expected<void, std::string> MoveItem(const base::FilePath& from,
                                           const base::FilePath& to) {
  if (ItemExists(to)) {
    return base::unexpected(base::StrCat({to.value(), ": already exists"}));
  }
  if (rename(from.value().c_str(), to.value().c_str()) == 0) {
    return base::ok();
  }
  if (errno != EXDEV) {
    return Failed(from);
  }
  RETURN_IF_ERROR(CopyItem(from, to));
  return RemoveItem(from);
}

base::expected<void, std::string> RemoveItem(const base::FilePath& path) {
  if (!ItemExists(path)) {
    return base::unexpected(base::StrCat({path.value(), ": no such file"}));
  }
  if (!base::DeletePathRecursively(path)) {
    return Failed(path);
  }
  return base::ok();
}

bool Touch(const base::FilePath& path) {
  return base::WriteFile(path, std::string_view());
}

base::FilePath StandardizedPath(const base::FilePath& path) {
  if (!path.IsAbsolute()) {
    return path;
  }
  std::vector<std::string_view> components;
  for (std::string_view component :
       base::SplitStringPiece(path.value(), "/", base::KEEP_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (component == ".") {
      continue;
    }
    if (component == "..") {
      if (!components.empty()) {
        components.pop_back();
      }
      continue;
    }
    components.push_back(component);
  }
  const std::string standardized =
      base::StrCat({"/", base::JoinString(components, "/")});

  static constexpr std::array<std::string_view, 3> kPrefixes = {
      "/private/var/automount", "/var/automount", "/private"};
  for (std::string_view prefix : kPrefixes) {
    if (standardized.size() > prefix.size() &&
        standardized.starts_with(prefix) &&
        standardized[prefix.size()] == '/') {
      const base::FilePath rest(standardized.substr(prefix.size()));
      if (base::PathExists(rest)) {
        return rest;
      }
    }
  }
  return base::FilePath(standardized);
}

}  // namespace pane::model
