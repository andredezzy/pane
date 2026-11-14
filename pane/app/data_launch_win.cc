// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/data_launch.h"

#include <windows.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/chrome_elf/chrome_elf_main.h"
#include "chrome/common/chrome_paths.h"
#include "pane/app/test_copy.h"
#include "pane/model/app_lock.h"
#include "pane/model/data_lock.h"
#include "pane/model/engine_folder.h"
#include "pane/model/engine_secret.h"
#include "pane/model/folder_place.h"
#include "pane/model/local_state.h"
#include "pane/model/model_store.h"
#include "pane/model/panic_wipe_run.h"
#include "pane/model/persisted_file.h"
#include "pane/model/pin_guard.h"
#include "pane/model/test_hooks.h"

namespace pane {

// Pane's data on Windows (U5): its folder in the user's local application
// data, kept out of the index; the data key sealed under os_crypt's DPAPI key
// and, with a PIN, a key the TPM holds; the engine's folder in it, User Data,
// and every profile in that by its id, each file encrypted under its Engine
// key (ADR 0121), with no volume around them, which would need elevation. No
// Pane ran on Windows before, so there is no earlier data to bring over.
//
// The engine's folder is the very one chrome_elf names. On Windows Chromium
// takes its user data dir from chrome_elf alone (ChromeMainDelegate's
// InitializeUserDataDir), which reads --user-data-dir from the command line
// the process started with, never from base::CommandLine, where
// SetEngineSwitches writes; else a policy's folder; else the install mode's,
// %LOCALAPPDATA%\Pane\User Data (patch 0001). A launch without the switch, as
// a shortcut, the taskbar, or another app's link starts the user's Pane and
// as the panic wipe's relaunch keeps it, gets that default: the engine folder
// of the data folder, %LOCALAPPDATA%\Pane (model::EngineFolder). A test copy
// names its own with the switch. Any other is refused before anything is
// written: an engine folder elsewhere took Chrome's whole user data, its
// Default profile and Local State in the clear, while Pane's stayed empty.

namespace {

// Where `path` leads: the deepest part of it that exists, as NTFS resolves it
// (long names for 8.3 ones, junctions followed, the case on disk), then the
// rest as given. The path as given where nothing of it resolves.
base::FilePath WhereItLeads(const base::FilePath& path) {
  std::vector<base::FilePath::StringType> rest;
  base::FilePath existing = path.StripTrailingSeparators();
  while (!base::PathExists(existing)) {
    const base::FilePath parent = existing.DirName();
    if (parent == existing) {
      return path;
    }
    rest.push_back(existing.BaseName().value());
    existing = parent;
  }
  base::FilePath resolved;
  if (!base::NormalizeFilePath(existing, &resolved)) {
    return path;
  }
  for (auto name = rest.rbegin(); name != rest.rend(); ++name) {
    resolved = resolved.Append(*name);
  }
  return resolved;
}

// Whether two absolute paths name one folder: as given or by where they lead,
// their names compared as NTFS compares them, without case, ASCII's
// (engine_keys_win.cc's SameComponent).
bool SameFolder(const base::FilePath& a, const base::FilePath& b) {
  const auto same = [](const base::FilePath& x, const base::FilePath& y) {
    return base::EqualsCaseInsensitiveASCII(
        x.NormalizePathSeparators().StripTrailingSeparators().value(),
        y.NormalizePathSeparators().StripTrailingSeparators().value());
  };
  return same(a, b) || same(WhereItLeads(a), WhereItLeads(b));
}

// Whether Chromium keeps its user data in `engine_folder` on this launch, by
// chrome_elf's own answer, which ChromeMainDelegate reads a moment later. A
// folder the command line named that chrome_elf couldn't use sends it to its
// default, which Chrome then reports: no launch Pane can be sure of either.
bool ChromiumKeepsItsDataIn(const base::FilePath& engine_folder) {
  std::array<wchar_t, MAX_PATH> named = {};
  std::array<wchar_t, MAX_PATH> unusable = {};
  if (!GetUserDataDirectoryThunk(named.data(), named.size(), unusable.data(),
                                 unusable.size()) ||
      unusable[0] != L'\0') {
    return false;
  }
  return SameFolder(base::FilePath(std::wstring_view(named.data())),
                    engine_folder);
}

// Windows Search, and any indexer that honors it, skips the data: the folder
// carries FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, which NTFS gives everything
// made in it after, in place of the Mac's Spotlight markers (ADR 0121).
void KeepOutOfTheIndex(const base::FilePath& folder) {
  const DWORD attributes = ::GetFileAttributesW(folder.value().c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    PLOG(ERROR) << "Could not read the data folder's attributes";
    return;
  }
  if ((attributes & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED) ||
      ::SetFileAttributesW(folder.value().c_str(),
                           attributes | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)) {
    return;
  }
  PLOG(ERROR) << "Could not keep the data folder out of the index";
}

// A test copy's home: the user's folders, which Chromium reads from the
// shell on Windows and no variable moves (Downloads, Documents, the Desktop,
// AppData), under the folder PANE_HOME_DIR names in the temporary folder, as
// a home of its own gives them on the Mac (HOME and CFFIXED_USER_HOME), so
// a scenario's downloads and saves are never the user's. Its data and its
// engine's stay where the launch names them.
void TakeTestCopyHome() {
  const std::optional<std::string> named = model::TestHook("PANE_HOME_DIR");
  if (!named) {
    return;
  }
  const base::FilePath home = base::FilePath::FromUTF8Unsafe(*named);
  if (!InTemporaryFolder(home)) {
    LOG(ERROR) << "A test copy's home lies outside the temporary folder";
    return;
  }
  if (!base::PathService::Override(base::DIR_HOME, home)) {
    LOG(ERROR) << "Could not give a test copy its home";
  }
  const std::pair<int, const base::FilePath::CharType*> folders[] = {
      {base::DIR_USER_DESKTOP, FILE_PATH_LITERAL("Desktop")},
      {base::DIR_ROAMING_APP_DATA, FILE_PATH_LITERAL("AppData\\Roaming")},
      {base::DIR_LOCAL_APP_DATA, FILE_PATH_LITERAL("AppData\\Local")},
      {chrome::DIR_USER_DOCUMENTS, FILE_PATH_LITERAL("Documents")},
      {chrome::DIR_USER_MUSIC, FILE_PATH_LITERAL("Music")},
      {chrome::DIR_USER_PICTURES, FILE_PATH_LITERAL("Pictures")},
      {chrome::DIR_USER_VIDEOS, FILE_PATH_LITERAL("Videos")},
      {chrome::DIR_DEFAULT_DOWNLOADS, FILE_PATH_LITERAL("Downloads")},
      {chrome::DIR_DEFAULT_DOWNLOADS_SAFE, FILE_PATH_LITERAL("Downloads")},
  };
  for (const auto& [key, folder] : folders) {
    if (!base::PathService::Override(key, home.Append(folder))) {
      LOG(ERROR) << "Could not give a test copy's home folder " << key;
    }
  }
}

// Whether profiles.json is sealed (its box): data a key opened, which no key
// then reads. A plain one, as an earlier app wrote it, opens with none and is
// sealed once read.
bool HoldsSealedProfiles(const base::FilePath& data_directory) {
  const base::expected<std::optional<base::Value>, model::FileError> read =
      model::ReadJsonFile(
          data_directory.Append(FILE_PATH_LITERAL("profiles.json")));
  if (!read.has_value()) {
    // There, but unreadable: kept shut rather than written over.
    return true;
  }
  return *read && (*read)->is_dict() && (*read)->GetDict().FindString("box");
}

// What Windows' wipe takes besides the data's own files: every key the TPM
// holds for this data's PIN, at once, so a copy of security.json made
// earlier opens nothing.
model::WipeExtras WindowsWipeExtras(scoped_refptr<model::PinGuard> guard) {
  model::WipeExtras extras;
  extras.at_once = base::BindRepeating(
      [](scoped_refptr<model::PinGuard> guard) {
        if (guard) {
          guard->Forget(std::nullopt);
        }
      },
      std::move(guard));
  return extras;
}

}  // namespace

base::FilePath DefaultDataDirectory() {
  base::FilePath local_app_data;
  base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local_app_data);
  return local_app_data.Append(FILE_PATH_LITERAL("Pane"));
}

// The Mac's order, less what only the Mac has: an earlier Pane's data, its
// caches apart, the volume.
std::optional<int> PrepareData(const base::FilePath& data_directory,
                               const base::CommandLine& command_line,
                               bool test_copy) {
  // The engine keeps its data in this data's engine folder, or nowhere.
  if (!ChromiumKeepsItsDataIn(model::EngineFolder(data_directory))) {
    LOG(ERROR) << "Pane refused a launch whose engine would keep its data "
                  "outside the data folder's engine folder";
    return 1;
  }

  // The data is the user's alone: %LOCALAPPDATA% is, and what is made in it
  // takes its ACL.
  if (!base::CreateDirectory(data_directory)) {
    LOG(ERROR) << "Could not make the data folder";
    return 1;
  }
  KeepOutOfTheIndex(data_directory);
  // chrome_elf makes a folder the command line names as it reads it, a test
  // copy's engine folder, before the data folder was marked: what the engine
  // makes in it takes the attribute from it.
  if (base::DirectoryExists(model::EngineFolder(data_directory))) {
    KeepOutOfTheIndex(model::EngineFolder(data_directory));
  }
  TakeTestCopyHome();

  // The panic wipe's data goes before anything reads the data directory: the
  // wiped instance started this one, or a wipe was cut short.
  const scoped_refptr<model::PinGuard> guard =
      model::PinGuard::ForData(data_directory);
  scoped_refptr<model::LaunchErase> erase = model::PanicWipeAtLaunch(
      data_directory, command_line, WindowsWipeExtras(guard));

  // Chromium opens the profile it used last at startup, before the PIN:
  // never one of Pane's.
  if (!model::StartOnDefaultProfile(model::EngineFolder(data_directory))) {
    LOG(ERROR) << "Could not point Local State at the Default profile";
  }

  // The Pane that runs on this data holds its lock until it ends. One started
  // while it runs hands its URLs over through the engine's singleton and
  // quits, making no secret of its own.
  std::unique_ptr<model::DataLock> data_lock =
      model::DataLock::Take(data_directory);

  // os_crypt's key, found, or made the first time, here, before the engine's
  // OSCrypt reads it: the data key is sealed under it. A sealed profiles.json
  // with no key to open it is data lost: the lock stays shut.
  model::AppLock::Loaded loaded = model::AppLock::Load(
      data_directory, /*holds_data=*/HoldsSealedProfiles(data_directory));
  model::AppLock::LaunchSecret secret;
  if (data_lock && !loaded.unreadable && !loaded.key_lost) {
    secret.secret = model::EngineSecret(model::EngineFolder(data_directory),
                                        /*makes_one=*/!loaded.state.key);
  }

  LaunchedData launched;
  launched.data_directory = data_directory;
  launched.place = base::MakeRefCounted<model::FolderPlace>(
      data_directory, std::move(data_lock));
  launched.erase = std::move(erase);
  launched.loaded = std::move(loaded);
  launched.secret = std::move(secret);
  launched.lock_system.secret = base::BindRepeating(
      &model::EngineSecret, model::EngineFolder(data_directory));
  launched.lock_system.guard = guard;
  launched.lock_system.no_secret =
      model::PinEntry::UnreadableReason::kDpapiRefused;
  launched.lock_system.wipe = base::BindRepeating(
      [](const base::FilePath& data_directory,
         scoped_refptr<model::PinGuard> guard) {
        model::WipeAndRelaunch(data_directory, WindowsWipeExtras(guard));
      },
      data_directory, guard);
  launched.settings = model::ModelStore::LoadSettings(data_directory);
  KeepLaunchedData(std::move(launched));
  return std::nullopt;
}

// No Pane ran on Windows before: nothing to convert.
model::FileMigrator MakeFileMigrator(const base::FilePath& user_data_dir) {
  return model::FileMigrator();
}

}  // namespace pane
