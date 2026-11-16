// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_FILES_H_
#define PANE_INSTALLER_FILES_H_

#include <windows.h>

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>

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

// Whether `folder` is the install's own app folder, AppFolder(), compared as
// NTFS compares names: the one folder an uninstall removes, and whose
// chrome.exe alone registers or unregisters Pane. Another folder that holds
// a chrome.exe (out/Win, a user's copy) never is, nor a path that names it
// another way (`..`, a short name).
bool IsInstalledAppFolder(const base::FilePath& folder);

// A folder removed, files and all, tried again for 2 s while something lets
// go of one (Windows Defender reads a new file, a helper exits): whether it
// has gone, and where it hasn't, the last error in `error`.
bool RemoveFolder(const base::FilePath& folder, DWORD* error = nullptr);

// Programs\Pane and Programs, where nothing else is in them: what a first
// install stopped or failed, or an uninstall, would otherwise leave
// (RemoveDirectory takes an empty folder alone).
void RemoveEmptyFolders();

// The helper, copied into a folder of its own in the temporary folder,
// `prefix` and 8 hex digits, so the folder it came from can be removed (the
// uninstall's app folder) or renamed (an update's staged folder) while it
// runs: the copy, or nothing.
std::optional<base::FilePath> CopyHelperOut(const base::FilePath& helper,
                                            std::wstring_view prefix);

// Starts what deletes this program's own copy once it has exited, where it
// runs from a folder CopyHelperOut made with `prefix`: a program can't
// delete its own file while it runs, so Windows' command shell does it, as
// Squirrel's uninstall deletes its own.
void DeleteOwnCopyAfterExit(std::wstring_view prefix);

// Whether `folder` is one CopyHelperOut made with `prefix`, and nothing
// else's: its name the prefix and 8 hex digits, and nothing in it but the
// helper's copy.
bool IsHelperCopy(const base::FilePath& folder, std::wstring_view prefix);

}  // namespace pane::installer

#endif  // PANE_INSTALLER_FILES_H_
