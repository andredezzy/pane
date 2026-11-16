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
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/prefs/profile_pref_store_manager.h"
#include "chrome/chrome_elf/chrome_elf_main.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/install_static/install_util.h"
#include "crypto/sha2.h"
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
#include "pane/model/preference_macs.h"
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

// Where Chromium keeps its user data on this launch, by chrome_elf's own
// answer, which ChromeMainDelegate reads a moment later: the folder it named,
// and the one the command line or a policy gave it that it couldn't use, which
// sends it to its default and which Chrome then reports.
struct ChromiumsFolder {
  base::FilePath named;
  base::FilePath unusable;

  // What Pane was given: the folder chrome_elf couldn't use, else the one it
  // named; none where chrome_elf answered nothing.
  base::FilePath Given() const { return unusable.empty() ? named : unusable; }
};

std::optional<ChromiumsFolder> ChromiumsUserDataFolder() {
  std::array<wchar_t, MAX_PATH> named = {};
  std::array<wchar_t, MAX_PATH> unusable = {};
  if (!GetUserDataDirectoryThunk(named.data(), named.size(), unusable.data(),
                                 unusable.size())) {
    return std::nullopt;
  }
  return ChromiumsFolder{base::FilePath(std::wstring_view(named.data())),
                         base::FilePath(std::wstring_view(unusable.data()))};
}

// Whether Chromium keeps its user data in `engine_folder` on this launch: a
// folder chrome_elf couldn't use is no launch Pane can be sure of either.
bool KeepsItsDataIn(const std::optional<ChromiumsFolder>& folder,
                    const base::FilePath& engine_folder) {
  return folder && folder->unusable.empty() &&
         SameFolder(folder->named, engine_folder);
}

// The refusal says why, and names the folder Pane was given, so the setting
// can be found (Paper 11.16): in Windows' own MessageBox, since nothing of
// Pane's can show this early, before any resource bundle, message loop, or
// widget, where Chrome falls back to it too (message_box_dialog.cc). Its OK,
// Enter, Esc, or Close end the launch. A scenario's run hears none of it, as
// none of the Mac's launch alerts (PANE_AUTOMATION_DIR), so no test copy
// waits on it; the log keeps its line either way.
void SayWhyRefused(const std::optional<ChromiumsFolder>& folder) {
  if (model::TestHook("PANE_AUTOMATION_DIR")) {
    return;
  }
  std::wstring text =
      L"Pane can\u2019t open with the data folder it was given.\n\n"
      L"A policy, or the shortcut or program that started Pane, points it to "
      L"another folder for its data, and Pane keeps your data only in its "
      L"own. Remove that setting, then open Pane again.";
  if (folder && !folder->Given().empty()) {
    text += L"\n\nThe folder it was given: " + folder->Given().value();
  }
  ::MessageBoxW(nullptr, text.c_str(), L"Pane",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
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

// Where a test copy's engine keeps its preference MACs: a key of its own,
// under this one, never the user's (PreferenceMacsKey).
constexpr wchar_t kTestCopiesKey[] = L"Software\\Pane Test Copies";

// The key the engine keeps its profiles' preference MACs under, in the
// registry (preference_macs.h), from PrepareData on: the pointer Chromium is
// handed for a test copy outlives every profile.
std::wstring& PreferenceMacsKey() {
  static base::NoDestructor<std::wstring> key;
  return *key;
}

// The user's Pane: Chromium's own key, install_static's Software\Pane, as
// Chromium names it with no word from Pane. A test copy: one of its own,
// under Software\Pane Test Copies, named by its data folder, so a scenario's
// profiles never reach the user's key and its deletes and wipes act on its
// own (ADR 0121); the scenario runner removes it once the scenario ends
// (tests/lib/tools.ts). It is named through the seam Chromium keeps for its
// own tests, which names the key for every profile from then on
// (ProfilePrefStoreManager::SetPreferenceValidationRegistryPathForTesting),
// before the first profile opens; the user's launch never calls it.
void NamePreferenceMacsKey(const base::FilePath& data_directory,
                           bool test_copy) {
  if (!test_copy) {
    PreferenceMacsKey() = install_static::GetRegistryPath();
    return;
  }
  const std::string name =
      base::HexEncode(crypto::SHA256HashString(data_directory.AsUTF8Unsafe()))
          .substr(0, 16);
  PreferenceMacsKey() =
      std::wstring(kTestCopiesKey) + L"\\" + base::ASCIIToWide(name);
  ProfilePrefStoreManager::SetPreferenceValidationRegistryPathForTesting(
      &PreferenceMacsKey());
}

// What Windows' wipe takes besides the data's own files: every key the TPM
// holds for this data's PIN, so a copy of security.json made earlier opens
// nothing, and every profile's preference MACs in the registry, at once.
model::WipeExtras WindowsWipeExtras(scoped_refptr<model::PinGuard> guard) {
  model::WipeExtras extras;
  extras.at_once = base::BindRepeating(
      [](scoped_refptr<model::PinGuard> guard, const std::wstring& macs) {
        if (guard) {
          guard->Forget(std::nullopt);
        }
        if (!model::ForgetEveryPreferenceMacs(macs)) {
          LOG(ERROR) << "Could not forget the preference MACs";
        }
      },
      std::move(guard), PreferenceMacsKey());
  return extras;
}

// The PIN screen's lines in Windows' words (Paper 11.7: Exit, and this PC),
// the chip's own states as 07.10 chose them: its lockout (07.1.1), a new PIN
// it wouldn't take (07.3.1), its key gone (07.6.1), the chip off (07.6.2),
// and DPAPI's refusal (07.7.1); and the chip refusing the current PIN in
// Settings (07.13 A, 07.4.2).
model::AppLock::System::Words WindowsLockWords() {
  model::AppLock::System::Words words;
  words.key_unreadable =
      "Pane can’t read the key to your data.\n"
      "Exit, then restore security.json.";
  words.no_secret =
      "Pane can’t read the key to your data.\n"
      "Windows keeps it for the account that set Pane up: open Pane there.";
  words.data_wont_open =
      "Pane can’t open your encrypted data.\n"
      "Exit, then open Pane again.";
  words.guard_gone =
      "Pane can’t read the key to your data.\n"
      "This PC’s security chip no longer has it, and nothing can bring it "
      "back.";
  words.guard_unavailable =
      "Pane can’t use this PC’s security chip.\n"
      "Turn it back on if it was turned off, then open Pane again.";
  words.locked_out =
      "Pane can’t check your PIN right now.\n"
      "Too many wrong PINs on this PC. Try again later.";
  words.pin_not_saved =
      "Pane couldn’t save the new PIN.\n"
      "This PC’s security chip refused it. Try again later.";
  words.guard_refused =
      "Pane can’t check your PIN right now.\n"
      "This PC’s security chip had an error. Try again later.";
  return words;
}

// A deleted profile's preference MACs, once its folder has gone.
model::FolderPlace::ForgetOutside ForgetProfileOutside() {
  return base::BindRepeating(
      [](const std::wstring& macs, const std::string& profile_id) {
        if (!model::ForgetPreferenceMacs(macs, profile_id)) {
          LOG(ERROR) << "Could not forget a deleted profile's preference MACs";
        }
      },
      PreferenceMacsKey());
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
  const std::optional<ChromiumsFolder> folder = ChromiumsUserDataFolder();
  if (!KeepsItsDataIn(folder, model::EngineFolder(data_directory))) {
    LOG(ERROR) << "Pane refused a launch whose engine would keep its data "
                  "outside the data folder's engine folder";
    SayWhyRefused(folder);
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
  // Before the wipe's at launch, which forgets them, and the first profile.
  NamePreferenceMacsKey(data_directory, test_copy);

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
      data_directory, std::move(data_lock), ForgetProfileOutside());
  launched.erase = std::move(erase);
  launched.loaded = std::move(loaded);
  launched.secret = std::move(secret);
  launched.lock_system.secret = base::BindRepeating(
      &model::EngineSecret, model::EngineFolder(data_directory));
  launched.lock_system.guard = guard;
  launched.lock_system.no_secret =
      model::PinEntry::UnreadableReason::kDpapiRefused;
  launched.lock_system.words = WindowsLockWords();
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
