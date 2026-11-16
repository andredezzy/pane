// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/files.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string_view>

#include "base/base_paths.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"

namespace pane::installer {

namespace {

constexpr uint64_t kKilobyte = 1024;
constexpr uint64_t kMegabyte = kKilobyte * 1024;
constexpr uint64_t kGigabyte = kMegabyte * 1024;

// What an install leaves free past its two copies, so the drive isn't filled
// to its last byte.
constexpr uint64_t kSpaceMargin = 64 * kMegabyte;

// The longest pane-version Pane reads (pane/app/version_win.cc).
constexpr int64_t kLongestVersion = 64;

// `bytes` in `unit`, rounded to `decimals` places, without a trailing ".0"
// below 10.
std::u16string Amount(uint64_t bytes, uint64_t unit, int decimals) {
  const double value = static_cast<double>(bytes) / static_cast<double>(unit);
  if (decimals == 0) {
    return base::NumberToString16(static_cast<uint64_t>(std::llround(value)));
  }
  return base::UTF8ToUTF16(base::StringPrintf("%.1f", value));
}

// The unit a size is shown in, and its places: GB with one, MB and KB with
// none.
struct Unit {
  uint64_t bytes;
  int decimals;
  std::u16string_view name;
};

Unit UnitFor(uint64_t bytes) {
  if (bytes >= kGigabyte) {
    return {kGigabyte, 1, u"GB"};
  }
  if (bytes >= kMegabyte) {
    return {kMegabyte, 0, u"MB"};
  }
  return {kKilobyte, 0, u"KB"};
}

}  // namespace

base::FilePath ProgramsFolder() {
  base::FilePath local;
  if (!base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local)) {
    return base::FilePath();
  }
  return local.Append(FILE_PATH_LITERAL("Programs"));
}

base::FilePath InstallFolder() {
  const base::FilePath programs = ProgramsFolder();
  return programs.empty() ? programs
                          : programs.Append(FILE_PATH_LITERAL("Pane"));
}

base::FilePath AppFolder() {
  const base::FilePath install = InstallFolder();
  return install.empty() ? install
                         : install.Append(FILE_PATH_LITERAL("Application"));
}

base::FilePath DataFolder() {
  base::FilePath local;
  if (!base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local)) {
    return base::FilePath();
  }
  return local.Append(FILE_PATH_LITERAL("Pane"));
}

std::optional<base::Version> VersionIn(const base::FilePath& app_folder) {
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(
          app_folder.Append(FILE_PATH_LITERAL("pane-version")), &contents,
          kLongestVersion)) {
    return std::nullopt;
  }
  const base::Version version(
      base::TrimWhitespaceASCII(contents, base::TRIM_ALL));
  if (!version.IsValid()) {
    return std::nullopt;
  }
  return version;
}

uint64_t SpaceNeeded(uint64_t unpacked_size) {
  return 2 * unpacked_size + kSpaceMargin;
}

std::optional<uint64_t> FreeSpace(const base::FilePath& path) {
  // The nearest folder of `path` that exists: the drive answers for it.
  base::FilePath existing = path;
  while (!base::DirectoryExists(existing)) {
    const base::FilePath parent = existing.DirName();
    if (parent == existing) {
      return std::nullopt;
    }
    existing = parent;
  }
  ULARGE_INTEGER available = {};
  if (!::GetDiskFreeSpaceExW(existing.value().c_str(), &available, nullptr,
                             nullptr)) {
    return std::nullopt;
  }
  return available.QuadPart;
}

std::u16string DriveOf(const base::FilePath& path) {
  const std::wstring& value = path.value();
  if (value.size() >= 2 && value[1] == L':') {
    return base::StrCat(
        {std::u16string(1, base::ToUpperASCII(static_cast<char16_t>(value[0]))),
         u":"});
  }
  return base::WideToUTF16(value.substr(0, value.find(L'\\', 2)));
}

std::u16string SizeText(uint64_t bytes) {
  const Unit unit = UnitFor(bytes);
  return base::StrCat(
      {Amount(bytes, unit.bytes, unit.decimals), u" ", unit.name});
}

std::u16string SizeOfText(uint64_t done, uint64_t total) {
  const Unit unit = UnitFor(total);
  return base::StrCat({Amount(std::min(done, total), unit.bytes, unit.decimals),
                       u" of ", Amount(total, unit.bytes, unit.decimals), u" ",
                       unit.name});
}

uint64_t FolderSize(const base::FilePath& folder) {
  uint64_t size = 0;
  WIN32_FIND_DATAW found = {};
  HANDLE find = ::FindFirstFileExW(
      folder.Append(L"*").value().c_str(), FindExInfoBasic, &found,
      FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
  if (find == INVALID_HANDLE_VALUE) {
    return 0;
  }
  do {
    const std::wstring_view name(found.cFileName);
    if (name == L"." || name == L"..") {
      continue;
    }
    if (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
      continue;
    }
    if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      size += FolderSize(folder.Append(found.cFileName));
    } else {
      size += (static_cast<uint64_t>(found.nFileSizeHigh) << 32) |
              found.nFileSizeLow;
    }
  } while (::FindNextFileW(find, &found));
  ::FindClose(find);
  return size;
}

bool IsInFolder(const base::FilePath& inner, const base::FilePath& folder) {
  const std::wstring a =
      inner.NormalizePathSeparators().StripTrailingSeparators().value();
  const std::wstring b =
      folder.NormalizePathSeparators().StripTrailingSeparators().value();
  if (b.empty() || a.size() < b.size() ||
      ::CompareStringOrdinal(a.data(), static_cast<int>(b.size()), b.data(),
                             static_cast<int>(b.size()),
                             /*bIgnoreCase=*/TRUE) != CSTR_EQUAL) {
    return false;
  }
  return a.size() == b.size() || a[b.size()] == L'\\';
}

bool IsInstalledAppFolder(const base::FilePath& folder) {
  const base::FilePath installed = AppFolder();
  return !installed.empty() && !folder.empty() &&
         IsInFolder(folder, installed) && IsInFolder(installed, folder);
}

bool RemoveFolder(const base::FilePath& folder, DWORD* error) {
  DWORD last = ERROR_SUCCESS;
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (!base::PathExists(folder) || base::DeletePathRecursively(folder)) {
      return true;
    }
    last = ::GetLastError();
    ::Sleep(100);
  }
  LOG(WARNING) << "Could not remove " << folder << ": " << last;
  if (error) {
    *error = last ? last : static_cast<DWORD>(ERROR_SHARING_VIOLATION);
  }
  return false;
}

void RemoveEmptyFolders() {
  ::RemoveDirectoryW(InstallFolder().value().c_str());
  ::RemoveDirectoryW(ProgramsFolder().value().c_str());
}

std::optional<base::FilePath> CopyHelperOut(const base::FilePath& helper,
                                            std::wstring_view prefix) {
  base::FilePath temp;
  if (!base::GetTempDir(&temp)) {
    return std::nullopt;
  }
  const base::FilePath folder = temp.Append(base::StrCat(
      {prefix,
       base::ASCIIToWide(base::HexEncode(base::RandBytesAsVector(4)))}));
  const base::FilePath copy = folder.Append(helper.BaseName());
  if (!base::CreateDirectory(folder) || !base::CopyFile(helper, copy)) {
    const DWORD error = ::GetLastError();
    base::DeletePathRecursively(folder);
    ::SetLastError(error);
    return std::nullopt;
  }
  return copy;
}

bool IsHelperCopy(const base::FilePath& folder, std::wstring_view prefix) {
  const std::wstring name = folder.BaseName().value();
  if (!base::StartsWith(name, prefix, base::CompareCase::INSENSITIVE_ASCII) ||
      name.size() != prefix.size() + 8 ||
      !std::all_of(name.begin() + prefix.size(), name.end(),
                   [](wchar_t c) { return base::IsHexDigit(c); })) {
    return false;
  }
  base::FileEnumerator inside(
      folder, /*recursive=*/false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = inside.Next(); !path.empty();
       path = inside.Next()) {
    if (!base::EqualsCaseInsensitiveASCII(path.BaseName().value(),
                                          L"pane_setup.exe")) {
      return false;
    }
  }
  return true;
}

void DeleteOwnCopyAfterExit(std::wstring_view prefix) {
  base::FilePath exe;
  base::FilePath temp;
  if (!base::PathService::Get(base::FILE_EXE, &exe) ||
      !base::GetTempDir(&temp)) {
    return;
  }
  base::FilePath folder = exe.DirName();
  base::NormalizeFilePath(folder, &folder);
  base::NormalizeFilePath(temp, &temp);
  if (!IsInFolder(folder, temp) || folder == temp ||
      !base::StartsWith(folder.BaseName().value(), prefix,
                        base::CompareCase::INSENSITIVE_ASCII)) {
    return;
  }
  base::FilePath shell;
  if (!base::PathService::Get(base::DIR_SYSTEM, &shell)) {
    return;
  }
  const std::wstring quoted = base::StrCat({L"\"", folder.value(), L"\""});
  // Up to 30 tries a second apart, until the folder is gone.
  const std::wstring command =
      base::StrCat({L"\"", shell.Append(L"cmd.exe").value(),
                    L"\" /d /q /c \"for /l %i in (1,1,30) do (rd /s /q ",
                    quoted, L" 2>nul & if not exist ", quoted,
                    L" (exit) else (ping -n 2 127.0.0.1 >nul))\""});
  base::LaunchOptions options;
  options.start_hidden = true;
  options.feedback_cursor_off = true;
  options.current_directory = temp;
  base::LaunchProcess(command, options);
}

}  // namespace pane::installer
