// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/data_launch.h"

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/base_paths.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/path_service.h"
#include "pane/app/app_defaults.h"
#include "pane/app/test_copy.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/profiles.h"
#include "pane/encryption/engine_keys.h"
#include "pane/encryption/profile_migration.h"
#include "pane/model/app_lock.h"
#include "pane/model/app_settings.h"
#include "pane/model/data_lock.h"
#include "pane/model/local_state.h"
#include "pane/model/mac/bundle_identifier.h"
#include "pane/model/mac/data_volume.h"
#include "pane/model/mac/electron_migration.h"
#include "pane/model/mac/launch_alert.h"
#include "pane/model/mac/launch_migration.h"
#include "pane/model/mac/pin_hash.h"
#include "pane/model/mac/proxy_password_store.h"
#include "pane/model/mac/safe_storage.h"
#include "pane/model/mac/volume_place.h"
#include "pane/model/mac/wipe_sources.h"
#include "pane/model/model_store.h"
#include "pane/model/panic_wipe.h"
#include "pane/model/panic_wipe_run.h"
#include "pane/model/pin_guard.h"
#include "pane/model/test_hooks.h"

namespace pane {

namespace {

using model::AppLock;
using model::TestHook;

// One of the user's folders, as Foundation names it: a fixed home's
// (CFFIXED_USER_HOME), a scenario's, when there is one.
base::FilePath UserDirectory(int key) {
  base::FilePath path;
  base::PathService::Get(key, &path);
  return path;
}

base::FilePath EngineDirectory(const base::FilePath& data_directory) {
  return data_directory.Append("Engine");
}

// For the log: how many, never what, since each names a path.
void LogFailures(std::string_view what, size_t count) {
  if (count > 0) {
    LOG(ERROR) << what << ": " << count;
  }
}

// The Electron app's data: a scenario's (PANE_ELECTRON_DATA_DIR), or its
// folder used last in Application Support, which a test copy looks at only in
// the temporary folder.
std::optional<model::ElectronSource> ElectronSourceFor(
    const base::FilePath& application_support,
    bool test_copy) {
  if (const std::optional<std::string> named =
          TestHook("PANE_ELECTRON_DATA_DIR")) {
    return model::ElectronSource{.directory = base::FilePath(*named)};
  }
  if (test_copy && !InTemporaryFolder(application_support)) {
    return std::nullopt;
  }
  return model::DefaultElectronSource(application_support);
}

// A profile's folder in the volume, `root` (resolved), converted in place by
// //pane/encryption's migration (ADR 0121), its key registered: a safety copy
// first, for a profile that has files. On a sequence that may block; a kill
// at any point loses nothing. `progress` gets the share done.
bool MigrateProfileFolder(const base::FilePath& root,
                          base::RepeatingCallback<void(double)> progress) {
  const base::FilePath profile = root.AppendASCII("Profile");
  const base::FilePath archive = root.AppendASCII("SafetyCopy");

  // A new profile has nothing to keep.
  if (base::PathExists(profile.AppendASCII("Preferences")) &&
      !base::PathExists(archive) && !WriteSafetyCopy(profile, archive)) {
    return false;
  }

  return MigrateProfileFiles(profile, root.AppendASCII("Cache"),
                             std::move(progress));
}

// The passwords an earlier Pane kept apart from its plain profiles.json, read
// and removed on the store's sequence: the Keychain may ask first.
model::ModelStore::KeptApart KeptApartPasswords(
    const base::FilePath& data_directory) {
  const model::ProxyPasswordStore passwords(data_directory,
                                            model::SafeStorage::ForProcess());
  model::ModelStore::KeptApart kept_apart;
  kept_apart.read = base::BindRepeating(
      [](const model::ProxyPasswordStore& passwords) {
        return passwords.Read();
      },
      passwords);
  kept_apart.remove = base::BindRepeating(
      [](const model::ProxyPasswordStore& passwords) { passwords.Delete(); },
      passwords);
  return kept_apart;
}

}  // namespace

// The engine's side of the vault's work on a profile's folder (ADR 0121),
// run on the profile's sequence: the conversion of an earlier Pane's files,
// the restore of its safety copy, and the repair after 0.18.0's migration,
// each with the profile's key registered first, as the Swift's engine ran
// them. Traced as the Swift app's engine traced them, for the scenarios.
model::FileMigrator MakeFileMigrator(const base::FilePath& user_data_dir) {
  model::FileMigrator migrator;
  migrator.migrate = base::BindRepeating(
      [](const base::FilePath& user_data_dir, const std::string& id,
         const model::Key& key, const base::FilePath& folder,
         base::RepeatingCallback<void(double)> progress) {
        PANE_TRACE("call", "PaneBrowserApplication.migrateContext",
                   base::DictValue().Set("identifier", id));
        RegisterEngineKey(user_data_dir, id, key);
        // Resolved: /var is /private/var, where the roots have both only as
        // the user data dir was given. No folder: paths from an empty root
        // would lead into the working directory.
        const base::FilePath root = base::MakeAbsoluteFilePath(folder);
        const bool migrated =
            !root.empty() && MigrateProfileFolder(root, std::move(progress));
        PANE_TRACE("callback",
                   "PaneBrowserApplication.migrateContext.completion",
                   base::DictValue().Set("migrated", migrated));
        return migrated;
      },
      user_data_dir);
  migrator.restore = base::BindRepeating(
      [](const base::FilePath& user_data_dir, const std::string& id,
         const model::Key& key, const base::FilePath& folder) {
        PANE_TRACE("call", "PaneBrowserApplication.restoreContext",
                   base::DictValue().Set("identifier", id));
        RegisterEngineKey(user_data_dir, id, key);
        const base::FilePath root = base::MakeAbsoluteFilePath(folder);
        return !root.empty() &&
               RestoreSafetyCopy(root.AppendASCII("Profile"),
                                 root.AppendASCII("SafetyCopy"));
      },
      user_data_dir);
  migrator.repair = base::BindRepeating(
      [](const base::FilePath& user_data_dir, const std::string& id,
         const model::Key& key, const base::FilePath& folder) {
        RegisterEngineKey(user_data_dir, id, key);
        const base::FilePath root = base::MakeAbsoluteFilePath(folder);
        const bool repaired =
            !root.empty() &&
            RepairMigratedProfile(root.AppendASCII("Profile"),
                                  root.AppendASCII("SafetyCopy"));
        PANE_TRACE("callback",
                   "PaneBrowserApplication.repairContext.completion",
                   base::DictValue().Set("repaired", repaired));
        return repaired;
      },
      user_data_dir);
  return migrator;
}

base::FilePath DefaultDataDirectory() {
  return UserDirectory(base::DIR_APP_DATA).Append(model::kBundleIdentifier);
}

// The order is main.swift's, step by step.
std::optional<int> PrepareData(const base::FilePath& data_directory,
                               const base::CommandLine& command_line,
                               bool test_copy) {
  const base::FilePath application_support = UserDirectory(base::DIR_APP_DATA);
  const base::FilePath caches = UserDirectory(base::DIR_CACHE);
  const model::SafeStorage safe_storage = model::SafeStorage::ForProcess();

  // A test copy reads and writes nothing outside the temporary folder: what
  // the launch does beside the data, it does there alone. The user's launch
  // does all of it, as the Swift's main.swift did.
  const auto may_touch = [test_copy](const base::FilePath& path) {
    return !test_copy || InTemporaryFolder(path);
  };

  // The profiles' caches' folder, when the data root is in Application
  // Support. A test copy's lie in its home in the temporary folder too, or
  // the launch ends here.
  const std::optional<base::FilePath> cache_directory =
      model::ProfileCacheDirectory(data_directory);
  if (cache_directory && !may_touch(*cache_directory)) {
    LOG(ERROR) << "A test copy's caches lie outside the temporary folder";
    return 1;
  }

  // An earlier Pane's data, under its former identifier's folders, moves to
  // this one's before anything reads it (ADR 0114). Scenarios name their own
  // data, or a home of their own (CFFIXED_USER_HOME), whose preferences are
  // still the user's: those stay.
  if (!TestHook("PANE_DATA_DIR") && may_touch(application_support) &&
      may_touch(caches)) {
    const bool fixed_home =
        base::Environment::Create()->HasVar("CFFIXED_USER_HOME");
    LogFailures(
        "Could not move an earlier Pane's data",
        model::MoveFormerData(application_support, caches,
                              fixed_home ? std::nullopt
                                         : std::make_optional<std::string>(
                                               model::kBundleIdentifier))
            .size());
  }

  // What an earlier Pane's URL sessions kept in the clear goes (ADR 0113).
  if (const base::FilePath library = base::apple::GetUserLibraryPath();
      may_touch(library)) {
    LogFailures("Could not remove an earlier Pane's session caches",
                model::RemoveSessionCaches(library).size());
  }

  // And what the system kept for the former identifier in the user's cache
  // folder (ADR 0115). That folder is the system's, beside the temporary
  // folder, not in it: a test copy skips this, every time.
  if (const std::optional<base::FilePath> user_caches =
          model::UserCacheDirectory();
      user_caches && may_touch(*user_caches)) {
    LogFailures("Could not remove an earlier Pane's caches",
                model::RemoveFormerUserCaches(*user_caches).size());
  }

  // The data and its caches are the user's alone: other accounts on this Mac
  // read them at 0755.
  for (const std::optional<base::FilePath>& directory :
       {std::make_optional(data_directory), cache_directory}) {
    if (directory && (!base::CreateDirectory(*directory) ||
                      !base::SetPosixFilePermissions(*directory, 0700))) {
      LOG(ERROR) << "Could not make a data folder the user's alone";
    }
  }

  RegisterAppDefaults();

  // What an earlier engine kept in the clear, which it keeps off now (ADR
  // 0113, 0115), goes with the erase below.
  LogFailures("Could not stage an earlier engine's traces",
              model::StageEngineTraces(data_directory).size());

  // The panic wipe's data goes before anything reads the data directory: the
  // wiped instance started this one, or a wipe was cut short.
  const scoped_refptr<model::VolumeTools> tools = model::VolumeTools::For(
      model::DataVolume(data_directory, cache_directory));
  scoped_refptr<model::LaunchErase> erase = model::PanicWipeAtLaunch(
      data_directory, command_line,
      model::MacWipeExtras(data_directory, tools, safe_storage));

  // The first launch after the Electron app brings its data over, from a
  // copy, before anything here reads the data directory: a test copy's, only
  // from the temporary folder.
  if (const std::optional<model::ElectronSource> electron =
          ElectronSourceFor(application_support, test_copy);
      electron && may_touch(electron->directory)) {
    const std::unique_ptr<model::LaunchAlert> alert =
        model::LaunchAlert::Create(
            /*silent=*/TestHook("PANE_AUTOMATION_DIR").has_value());
    if (const std::optional<int> quit = model::RunLaunchMigration(
            *electron, data_directory, safe_storage, *alert)) {
      return quit;
    }
  }

  if (!model::DropPendingExtensions(data_directory)) {
    LOG(ERROR) << "Could not write the migration record again";
  }

  // Chromium opens the profile it used last at startup, before the PIN:
  // never one of Pane's, whose data is in the volume the data key opens (ADR
  // 0113).
  if (!model::StartOnDefaultProfile(EngineDirectory(data_directory))) {
    LOG(ERROR) << "Could not point Local State at the Default profile";
  }

  // The Pane that runs on this data holds its lock until it ends. One started
  // while it runs, which hands its URLs over through the engine's singleton
  // and quits, or while it quits, leaves its image alone (DataLock, ADR 0113).
  std::unique_ptr<model::DataLock> data_lock =
      model::DataLock::Take(data_directory);

  // The data image attaches now, its volume locked, while the engine starts:
  // only the unlock and the mount wait for the key (ADR 0113). On a thread of
  // its own, since no thread pool exists yet.
  scoped_refptr<model::LaunchAttach> attach;
  if (data_lock) {
    attach = model::LaunchAttach::Start(
        model::DataVolume(data_directory, cache_directory), tools,
        model::VolumePlace::WatchdogPath());
  }

  // The Keychain's secret, found, or made the first time, once, here, before
  // the engine's OSCrypt reads it on the thread pool: Chromium's GetPassword,
  // finding none, adds its own, and updates an item that appeared since, so
  // two at once would leave the Keychain one secret and the data key sealed
  // under the other. Never made once a key is sealed, which a new secret
  // would not open, nor looked for while the lock stays shut (a key that
  // won't read, or a volume with no key), nor by a Pane another holds the
  // data for: it hands its URLs over and quits, asking the Keychain nothing,
  // as the Swift's did, or waits for that one, which made the secret. The
  // lock opens the key with it, asking the Keychain nothing more, and makes
  // none of its own.
  AppLock::Loaded loaded = AppLock::Load(
      data_directory,
      /*holds_data=*/model::DataVolume(data_directory, std::nullopt).Exists());
  AppLock::LaunchSecret secret;
  if (data_lock && !loaded.unreadable && !loaded.key_lost) {
    secret.secret = safe_storage.DataSecret(/*makes_one=*/!loaded.state.key);
  }

  // The model starts with its settings alone (ADR 0113): read here, after the
  // Electron app's were brought over, while the disk may be read.
  model::AppSettings settings = model::ModelStore::LoadSettings(data_directory);

  // An earlier Pane's plain profile folders, which move into the volume
  // behind the panel from the first frame when no PIN is asked first.
  const bool has_plain_profiles =
      model::DataVolume(data_directory, cache_directory).HasPlainProfiles();

  LaunchedData launched;
  launched.data_directory = data_directory;
  launched.place = base::MakeRefCounted<model::VolumePlace>(
      data_directory, cache_directory, tools, std::move(data_lock),
      std::move(attach));
  launched.erase = std::move(erase);
  launched.loaded = std::move(loaded);
  launched.secret = std::move(secret);
  launched.lock_system.secret = base::BindRepeating(
      [](model::SafeStorage safe_storage, bool makes_one) {
        return safe_storage.DataSecret(makes_one);
      },
      safe_storage);
  launched.lock_system.matches_hash =
      base::BindRepeating([](std::string_view pin, std::string_view hash) {
        return model::VerifyPinHash(pin, hash);
      });
  launched.lock_system.guard = model::PinGuard::ForData(data_directory);
  launched.lock_system.no_secret =
      model::PinEntry::UnreadableReason::kKeychainDenied;
  // What it erases is read as it wipes: the Electron app's record, say.
  launched.lock_system.wipe = base::BindRepeating(
      [](const base::FilePath& data_directory,
         scoped_refptr<model::VolumeTools> tools,
         model::SafeStorage safe_storage) {
        model::WipeAndRelaunch(
            data_directory,
            model::MacWipeExtras(data_directory, tools, safe_storage));
      },
      data_directory, tools, safe_storage);
  launched.settings = std::move(settings);
  launched.kept_apart = KeptApartPasswords(data_directory);
  launched.has_plain_profiles = has_plain_profiles;
  KeepLaunchedData(std::move(launched));
  return std::nullopt;
}

}  // namespace pane
