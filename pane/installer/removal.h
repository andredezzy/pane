// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_REMOVAL_H_
#define PANE_INSTALLER_REMOVAL_H_

#include <windows.h>

#include <stdint.h>

#include <optional>

#include "base/files/file_path.h"
#include "base/functional/callback.h"

namespace pane::installer {

// The uninstall's removal of Pane (Paper 11.12.10), apart from its window
// (flow.cc's UninstallFlow), for its tests: the one place the uninstall
// unregisters and deletes. Each step checks again that the folder is the
// install's own (files.h's IsInstalledAppFolder) and refuses otherwise,
// whatever an earlier step checked: a folder elsewhere that holds a
// chrome.exe, a build's or a user's copy, is never unregistered or deleted.

// What a removal came to.
enum class RemovalOutcome {
  // Nothing of Pane's left: its registrations, the app folder, and the update
  // staged beside it.
  kRemoved,
  // Not the install's own folder: nothing unregistered or deleted.
  kRefused,
  // A Pane holds the data (chrome.exe's kSwitchPaneOpen): nothing changed.
  kPaneOpen,
  // chrome.exe's unregister failed: the app's files are untouched, and
  // Installed apps keeps its entry (windows_integration.h's Unregister), so
  // the uninstall runs again from there.
  kNotUnregistered,
  // Unregistered, Installed apps' entry gone, but some of the app's files or
  // folders stayed: what is left goes on a Try again, with no chrome.exe.
  kFilesLeft,
};

struct RemovalResult {
  RemovalOutcome outcome = RemovalOutcome::kRefused;
  // The failed step's code.
  DWORD error = ERROR_SUCCESS;
  // Whether the registrations are gone, so a Try again removes only files.
  bool unregistered = false;
};

// chrome.exe's unregister in the app folder: its exit code, or nothing where
// it didn't end (flow.cc runs chrome.exe; the tests, a stand-in).
using RunUnregister =
    base::OnceCallback<std::optional<int>(const base::FilePath& app)>;

// Told each removed file's size, for the bar.
using FileRemoved = base::RepeatingCallback<void(uint64_t size)>;

// Removes Pane from `app`: chrome.exe's unregister, unless `unregistered`
// (a Try again after it was done), then each of the app's files, the folder,
// the update folder beside it, and the folders Pane made where nothing else
// is in them. Refuses unless `app` is the install's own folder, checked
// before the unregister and again before the files.
RemovalResult RemovePane(const base::FilePath& app,
                         bool unregistered,
                         RunUnregister unregister,
                         const FileRemoved& removed);

}  // namespace pane::installer

#endif  // PANE_INSTALLER_REMOVAL_H_
