// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_FILES_H_
#define PANE_INSTALLER_FILES_H_

#include <stdint.h>

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/version.h"

namespace pane::installer {

// Where Pane lives on Windows (ADR 0127): the app in
// %LOCALAPPDATA%\Programs\Pane\Application, the data apart in
// %LOCALAPPDATA%\Pane (ADR 0121). Empty paths where Windows names no local
// application data.
base::FilePath ProgramsFolder();  // %LOCALAPPDATA%\Programs
base::FilePath InstallFolder();   // ...\Programs\Pane
base::FilePath AppFolder();       // ...\Programs\Pane\Application
base::FilePath DataFolder();      // %LOCALAPPDATA%\Pane

// The version an app folder's pane-version names, as Pane reads it
// (pane/app/version_win.cc): nothing where there is none.
std::optional<base::Version> VersionIn(const base::FilePath& app_folder);

// The space an install asks of the drive: the app folder twice, once for
// the folder and once for the update it will stage beside it (Paper 11.12.7),
// which also covers what the install holds while it unpacks, and a margin.
uint64_t SpaceNeeded(uint64_t unpacked_size);

// What the drive that holds `path` has free for this user, or nothing.
std::optional<uint64_t> FreeSpace(const base::FilePath& path);

// The drive's name as Windows writes it: "C:".
std::u16string DriveOf(const base::FilePath& path);

// A size as Windows shows one, in its binary units: "212 MB", "3.4 GB",
// "12 KB".
std::u16string SizeText(uint64_t bytes);

// A share of a size, both in the total's unit: "1.3 of 3.4 GB".
std::u16string SizeOfText(uint64_t done, uint64_t total);

// Every file's size under `folder`, through no link or junction.
uint64_t FolderSize(const base::FilePath& folder);

// `path` with every name compared as NTFS compares them: whether `inner` is
// `folder` or under it.
bool IsInFolder(const base::FilePath& inner, const base::FilePath& folder);

}  // namespace pane::installer

#endif  // PANE_INSTALLER_FILES_H_
