// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/update_folder.h"

#include <windows.h>

#include <atomic>
#include <iterator>
#include <map>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "pane/installer/chrome_exe.h"
#include "pane/installer/files.h"
#include "pane/installer/payload.h"
#include "pane/installer/switches.h"
#include "pane/installer/unpack.h"

namespace pane::installer {

namespace {

constexpr wchar_t kUpdate[] = L"Update";
constexpr wchar_t kSwapping[] = L"Swapping";
constexpr wchar_t kNotInstalled[] = L"not-installed";
constexpr std::wstring_view kPartial = L".partial";
constexpr std::wstring_view kRemoving = L".removing";
constexpr std::wstring_view kDownload = L".download";

// The longest record read: a path, at most Windows' longest.
constexpr size_t kLongestRecord = 32768 * 3;

base::FilePath Named(const base::FilePath& update_folder,
                     std::string_view version,
                     std::wstring_view suffix) {
  return update_folder.Append(
      base::StrCat({base::ASCIIToWide(version), suffix}));
}

// `name` without `suffix`, where it ends in it and what's left is a version.
std::optional<std::string> VersionBefore(std::wstring_view name,
                                         std::wstring_view suffix) {
  if (!name.ends_with(suffix)) {
    return std::nullopt;
  }
  const std::wstring_view rest = name.substr(0, name.size() - suffix.size());
  if (!base::IsStringASCII(rest)) {
    return std::nullopt;
  }
  std::string version = base::WideToASCII(rest);
  if (!IsStagedVersion(version)) {
    return std::nullopt;
  }
  return version;
}

bool SameName(const base::FilePath& path, std::wstring_view name) {
  return base::EqualsCaseInsensitiveASCII(path.BaseName().value(), name);
}

}  // namespace

UpdateFolderScan::UpdateFolderScan() = default;
UpdateFolderScan::UpdateFolderScan(const UpdateFolderScan&) = default;
UpdateFolderScan& UpdateFolderScan::operator=(const UpdateFolderScan&) =
    default;
UpdateFolderScan::~UpdateFolderScan() = default;

KeptUpdate::KeptUpdate() = default;
KeptUpdate::KeptUpdate(const KeptUpdate&) = default;
KeptUpdate& KeptUpdate::operator=(const KeptUpdate&) = default;
KeptUpdate::~KeptUpdate() = default;

base::FilePath UpdateFolderOf(const base::FilePath& app_folder) {
  return app_folder.StripTrailingSeparators().DirName().Append(kUpdate);
}

base::FilePath StagedFolder(const base::FilePath& update_folder,
                            std::string_view version) {
  return Named(update_folder, version, L"");
}

base::FilePath PartialFolder(const base::FilePath& update_folder,
                             std::string_view version) {
  return Named(update_folder, version, kPartial);
}

base::FilePath RemovingFolder(const base::FilePath& update_folder,
                              std::string_view version) {
  return Named(update_folder, version, kRemoving);
}

base::FilePath DownloadRecord(const base::FilePath& update_folder,
                              std::string_view version) {
  return Named(update_folder, version, kDownload);
}

base::FilePath SwappingFolder(const base::FilePath& update_folder) {
  return update_folder.Append(kSwapping);
}

base::FilePath NotInstalledMark(const base::FilePath& update_folder) {
  return update_folder.Append(kNotInstalled);
}

bool IsStagedVersion(std::string_view version) {
  if (version.empty() || version.size() > 31 ||
      !base::ContainsOnlyChars(version, "0123456789.")) {
    return false;
  }
  return base::Version(version).IsValid();
}

bool IsInstallLayout(const base::FilePath& app_folder) {
  const base::FilePath app = app_folder.StripTrailingSeparators();
  return SameName(app, L"Application") && SameName(app.DirName(), L"Pane");
}

std::optional<base::FilePath> ReadDownloadRecord(const base::FilePath& record) {
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(record, &contents, kLongestRecord)) {
    return std::nullopt;
  }
  const base::FilePath download = base::FilePath::FromUTF8Unsafe(
      base::TrimWhitespaceASCII(contents, base::TRIM_ALL));
  if (download.empty() || !download.IsAbsolute() ||
      download.ReferencesParent() || !base::PathExists(download) ||
      base::DirectoryExists(download)) {
    return std::nullopt;
  }
  return download;
}

bool WriteDownloadRecord(const base::FilePath& record,
                         const base::FilePath& download) {
  return base::WriteFile(record, download.AsUTF8Unsafe());
}

UpdateFolderScan ScanUpdateFolder(const base::FilePath& update_folder,
                                  const base::Version& running) {
  UpdateFolderScan scan;
  // Each staged version whole, and each record, by version.
  std::map<base::Version, std::pair<std::string, base::FilePath>> staged;
  std::map<std::string, base::FilePath> records;
  base::FileEnumerator entries(
      update_folder, /*recursive=*/false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = entries.Next(); !path.empty();
       path = entries.Next()) {
    const bool folder = entries.GetInfo().IsDirectory();
    const std::wstring name = path.BaseName().value();
    if (folder && SameName(path, kSwapping)) {
      scan.leftovers.push_back(path);
    } else if (!folder && SameName(path, kNotInstalled)) {
      scan.not_installed = true;
    } else if (folder && (VersionBefore(name, kPartial) ||
                          VersionBefore(name, kRemoving))) {
      scan.leftovers.push_back(path);
    } else if (std::optional<std::string> version =
                   folder ? VersionBefore(name, L"") : std::nullopt) {
      // Whole: its pane-version names it, and its chrome.exe is there.
      if (VersionIn(path) == base::Version(*version) &&
          base::PathExists(ChromeIn(path))) {
        staged.emplace(base::Version(*version), std::pair(*version, path));
      } else {
        scan.leftovers.push_back(path);
      }
    } else if (std::optional<std::string> recorded =
                   folder ? std::nullopt : VersionBefore(name, kDownload)) {
      records.emplace(*recorded, path);
    }
  }

  // The newest, where it is newer than the one running.
  if (!staged.empty() && running.IsValid() &&
      staged.rbegin()->first > running) {
    scan.kept = staged.rbegin()->second.first;
    staged.erase(std::prev(staged.end()));
  }
  for (const auto& [version, named] : staged) {
    scan.leftovers.push_back(named.second);
  }
  for (const auto& [version, record] : records) {
    if (version == scan.kept) {
      continue;
    }
    if (std::optional<base::FilePath> download = ReadDownloadRecord(record)) {
      scan.leftover_downloads.push_back(*download);
    }
    scan.leftovers.push_back(record);
  }
  return scan;
}

std::optional<KeptUpdate> CleanUpdateFolder(const base::FilePath& update_folder,
                                            const base::Version& running) {
  if (!base::DirectoryExists(update_folder)) {
    return std::nullopt;
  }
  const UpdateFolderScan scan = ScanUpdateFolder(update_folder, running);
  for (const base::FilePath& leftover : scan.leftovers) {
    base::DeletePathRecursively(leftover);
  }
  for (const base::FilePath& download : scan.leftover_downloads) {
    base::DeleteFile(download);
  }
  if (scan.not_installed) {
    base::DeleteFile(NotInstalledMark(update_folder));
  }
  if (!scan.kept) {
    ::RemoveDirectoryW(update_folder.value().c_str());
    return std::nullopt;
  }
  KeptUpdate kept;
  kept.version = *scan.kept;
  kept.staged = StagedFolder(update_folder, kept.version);
  kept.download =
      ReadDownloadRecord(DownloadRecord(update_folder, kept.version))
          .value_or(base::FilePath());
  kept.not_installed = scan.not_installed;
  return kept;
}

std::optional<base::FilePath> StageUpdate(const base::FilePath& installer,
                                          std::string_view version,
                                          const base::FilePath& update_folder) {
  if (!IsStagedVersion(version)) {
    return std::nullopt;
  }
  std::optional<Payload> payload;
  {
    base::File program(installer, base::File::FLAG_OPEN |
                                      base::File::FLAG_READ |
                                      base::File::FLAG_WIN_SHARE_DELETE);
    if (program.IsValid()) {
      payload = FindPayload(program);
    }
  }
  if (!payload || payload->version != version) {
    LOG(ERROR) << "The update's installer holds no Pane " << version;
    return std::nullopt;
  }

  const base::FilePath partial = PartialFolder(update_folder, version);
  base::DeletePathRecursively(partial);
  if (!base::CreateDirectory(partial)) {
    PLOG(ERROR) << "Could not make the update's folder";
    return std::nullopt;
  }
  const std::atomic<bool> cancel{false};
  const UnpackResult unpacked =
      Unpack(installer, *payload, partial, /*progress=*/nullptr, cancel);
  if (unpacked.outcome != UnpackResult::Outcome::kDone ||
      VersionIn(partial) != base::Version(version) ||
      !base::PathExists(ChromeIn(partial)) ||
      !base::PathExists(partial.Append(kHelperName))) {
    LOG(ERROR) << "The update didn't unpack whole: " << unpacked.error;
    base::DeletePathRecursively(partial);
    return std::nullopt;
  }
  const base::FilePath staged = StagedFolder(update_folder, version);
  const base::FilePath record = DownloadRecord(update_folder, version);
  base::DeletePathRecursively(staged);
  if (!base::Move(partial, staged) || !WriteDownloadRecord(record, installer)) {
    PLOG(ERROR) << "Could not stage the update";
    base::DeletePathRecursively(partial);
    base::DeletePathRecursively(staged);
    base::DeleteFile(record);
    return std::nullopt;
  }
  return staged;
}

void UnstageUpdate(const base::FilePath& staged,
                   const base::FilePath& download) {
  if (!staged.empty()) {
    const base::FilePath update_folder = staged.DirName();
    const std::string version = staged.BaseName().AsUTF8Unsafe();
    const base::FilePath removing = RemovingFolder(update_folder, version);
    base::DeletePathRecursively(removing);
    base::DeletePathRecursively(base::Move(staged, removing) ? removing
                                                             : staged);
    base::DeleteFile(DownloadRecord(update_folder, version));
  }
  if (!download.empty()) {
    base::DeleteFile(download);
  }
}

}  // namespace pane::installer
