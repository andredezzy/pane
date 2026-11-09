// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/data_launch.h"

#import <Foundation/Foundation.h>
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
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/path_service.h"
#include "base/strings/cstring_view.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "pane/app/test_copy.h"
#include "pane/model/local_state.h"
#include "pane/model/mac/app_lock.h"
#include "pane/model/mac/bundle_identifier.h"
#include "pane/model/mac/data_lock.h"
#include "pane/model/mac/data_vault.h"
#include "pane/model/mac/data_volume.h"
#include "pane/model/mac/electron_migration.h"
#include "pane/model/mac/launch_alert.h"
#include "pane/model/mac/launch_migration.h"
#include "pane/model/mac/panic_wipe.h"
#include "pane/model/mac/panic_wipe_run.h"
#include "pane/model/mac/safe_storage.h"
#include "pane/model/pin_entry.h"
#include "pane/model/security.h"

namespace pane {

namespace {

using model::AppLock;
using model::DataVault;

// A scenario's variable: on the mock keychain alone, whose secret opens none
// of the user's data (ADR 0115), as the Swift app's TestHooks read them.
std::optional<std::string> TestHook(base::cstring_view name) {
  if (!model::SafeStorage::UsesMockKeychain()) {
    return std::nullopt;
  }
  std::optional<std::string> value = base::Environment::Create()->GetVar(name);
  if (!value || value->empty()) {
    return std::nullopt;
  }
  return value;
}

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
// folder used last in Application Support, which, under the Views launch's
// guard (test_copy.h), is looked at only in the temporary folder.
std::optional<model::ElectronSource> ElectronSourceFor(
    const base::FilePath& application_support) {
  if (const std::optional<std::string> named =
          TestHook("PANE_ELECTRON_DATA_DIR")) {
    return model::ElectronSource{.directory = base::FilePath(*named)};
  }
  if (!InTemporaryFolder(application_support)) {
    return std::nullopt;
  }
  return model::DefaultElectronSource(application_support);
}

// What the launch kept for the vault and the lock, which open once the thread
// pool runs.
struct Launched {
  base::FilePath data_directory;
  std::optional<base::FilePath> cache_directory;
  scoped_refptr<model::VolumeTools> tools;
  // Held from launch; null when another Pane held it then.
  std::unique_ptr<model::DataLock> data_lock;
  // The image's attach at launch; null without the data's lock.
  scoped_refptr<model::LaunchAttach> attach;
  // The panic wipe's erase at launch, while it runs; null with none.
  scoped_refptr<model::LaunchErase> erase;
  // security.json, read once.
  AppLock::Loaded loaded;
  // The Keychain's secret, as the launch looked for it: the lock never makes
  // one.
  AppLock::LaunchSecret secret;
};

Launched* g_launched = nullptr;

std::string PhaseName(const DataVault& vault) {
  switch (vault.phase()) {
    case DataVault::Phase::kClosed:
      return "closed";
    case DataVault::Phase::kMoving:
      return base::StrCat(
          {"moving ", base::NumberToString(vault.share().value_or(0))});
    case DataVault::Phase::kOpen:
      return "open";
  }
}

// The PIN screen, as the Swift app's channel reported it.
base::Value Screen(const std::optional<model::PinEntry>& entry) {
  if (!entry) {
    return base::Value();
  }
  std::string_view mode;
  switch (entry->mode()) {
    case model::PinEntry::Mode::kSetup:
      mode = "setup";
      break;
    case model::PinEntry::Mode::kChange:
      mode = "change";
      break;
    case model::PinEntry::Mode::kRemove:
      mode = "remove";
      break;
    case model::PinEntry::Mode::kUnlock:
      mode = "unlock";
      break;
  }
  std::string_view step;
  switch (entry->step()) {
    case model::PinEntry::Step::kVerify:
      step = "verify";
      break;
    case model::PinEntry::Step::kEnter:
      step = "enter";
      break;
    case model::PinEntry::Step::kConfirm:
      step = "confirm";
      break;
  }
  const std::optional<std::string_view> title = entry->Title();
  return base::Value(
      base::DictValue()
          .Set("mode", mode)
          .Set("step", step)
          .Set("title", title ? base::Value(*title) : base::Value())
          .Set("dots", entry->Dots())
          .Set("entered", static_cast<int>(entry->entered().size()))
          .Set("busy", entry->busy())
          .Set("error",
               entry->error() ? base::Value(*entry->error()) : base::Value()));
}

// The vault and the lock, from the thread pool's start to the engine's stop,
// and what each tells the app. The profiles, the model, and the Lock's UI come
// in the next steps (docs/plans/views-migration.md, C6 and Phase 3): until
// then the vault holds no profile, and the lock opens the data key without a
// PIN alone. An earlier Pane's profiles moving in show from the first frame
// (DataVault::ExpectMove) once the profiles load.
class PaneData : public DataVault::Delegate, public AppLock::Delegate {
 public:
  explicit PaneData(Launched launched)
      : data_directory_(launched.data_directory),
        erase_(std::move(launched.erase)),
        vault_(launched.data_directory,
               launched.cache_directory,
               std::move(launched.tools),
               std::move(launched.data_lock),
               std::move(launched.attach),
               this),
        lock_(launched.data_directory,
              std::move(launched.loaded),
              vault_,
              model::SafeStorage::ForProcess(),
              this,
              std::move(launched.secret)) {}
  PaneData(const PaneData&) = delete;
  PaneData& operator=(const PaneData&) = delete;
  ~PaneData() override = default;

  const base::FilePath& data_directory() const { return data_directory_; }

  // Without a PIN the data key opens at once, off this sequence: the volume
  // is made the first time, attached, and opened.
  void Start() { lock_.Start(); }

  void EngineDidStop() { vault_.EngineDidStop(); }

  base::DictValue State() const {
    const model::SecurityState& state = lock_.state();
    base::ListValue failures;
    if (erase_) {
      for (const std::string& failure : erase_->Failures()) {
        failures.Append(failure);
      }
    }
    base::Value attached_at;
    if (const std::optional<DataVault::AttachTimes>& times =
            vault_.attached_at()) {
      attached_at = base::Value(base::DictValue()
                                    .Set("started", times->started)
                                    .Set("ended", times->ended));
    }
    return base::DictValue()
        .Set("pinLength",
             state.pin ? base::Value(state.pin->length) : base::Value())
        .Set("failedAttempts", state.failed_attempts)
        .Set("locked", lock_.is_locked())
        .Set("wipe", base::DictValue()
                         .Set("erasing", erase_ && erase_->IsErasing())
                         .Set("failures", std::move(failures)))
        .Set("data", base::DictValue()
                         .Set("phase", PhaseName(vault_))
                         .Set("attached", vault_.is_attached())
                         .Set("failure", vault_.failure()
                                             ? base::Value(*vault_.failure())
                                             : base::Value())
                         .Set("attachedAt", std::move(attached_at)))
        .Set("screen", Screen(lock_.entry()));
  }

  // DataVault::Delegate:
  void DataKeyDidOpen() override {}
  std::vector<std::string> ProfileIds() override { return {}; }
  void AccessDidOpen() override {}
  void VolumeWontOpen() override { lock_.VolumeWontOpen(); }
  void QuitToCloseVolume() override { chrome::AttemptExit(); }
  void VolumeDidDetachAtLock() override {}

  // AppLock::Delegate:
  void DidUnlock() override {}
  void DidExhaustSettingsAttempts() override {}

 private:
  const base::FilePath data_directory_;
  const scoped_refptr<model::LaunchErase> erase_;
  DataVault vault_;
  AppLock lock_;
};

PaneData* g_data = nullptr;

}  // namespace

base::FilePath DataDirectory() {
  if (const std::optional<std::string> named = TestHook("PANE_DATA_DIR")) {
    return base::FilePath(*named);
  }
  return UserDirectory(base::DIR_APP_DATA).Append(model::kBundleIdentifier);
}

// The order is main.swift's, step by step.
std::optional<int> PrepareData(const base::FilePath& data_directory,
                               const base::CommandLine& command_line) {
  CHECK(!g_launched);
  const base::FilePath application_support = UserDirectory(base::DIR_APP_DATA);
  const base::FilePath caches = UserDirectory(base::DIR_CACHE);
  const model::SafeStorage safe_storage = model::SafeStorage::ForProcess();

  // The profiles' caches' folder, when the data root is in Application
  // Support. The guard put the data root in the temporary folder, and its
  // caches go there too, or the launch ends here.
  const std::optional<base::FilePath> cache_directory =
      model::ProfileCacheDirectory(data_directory);
  if (cache_directory && !InTemporaryFolder(*cache_directory)) {
    LOG(ERROR) << "Pane's Views UI runs only as a test copy for now: its "
                  "caches outside the temporary folder";
    return 1;
  }

  // An earlier Pane's data, under its former identifier's folders, moves to
  // this one's before anything reads it (ADR 0114). Scenarios name their own
  // data, or a home of their own (CFFIXED_USER_HOME), whose preferences are
  // still the user's: those stay. Under the guard, only in a home in the
  // temporary folder, and never otherwise.
  if (!TestHook("PANE_DATA_DIR") && InTemporaryFolder(application_support) &&
      InTemporaryFolder(caches)) {
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
  // Under the guard, only in a home in the temporary folder.
  if (const base::FilePath library = base::apple::GetUserLibraryPath();
      InTemporaryFolder(library)) {
    LogFailures("Could not remove an earlier Pane's session caches",
                model::RemoveSessionCaches(library).size());
  }

  // And what the system kept for the former identifier in the user's cache
  // folder (ADR 0115). That folder is the system's, beside the temporary
  // folder, not in it: under the guard this is skipped, every time.
  if (const std::optional<base::FilePath> user_caches =
          model::UserCacheDirectory();
      user_caches && InTemporaryFolder(*user_caches)) {
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

  // Pane restores its own session (profiles.json), so AppKit's window
  // restoration stays off, along with its "reopen windows?" alert after a
  // crash. And the app's text as the Electron app drew its UI (CSS
  // -webkit-font-smoothing: antialiased): no font smoothing. Registered, so
  // in memory only.
  [NSUserDefaults.standardUserDefaults registerDefaults:@{
    @"ApplePersistenceIgnoreState" : @YES,
    @"AppleFontSmoothing" : @0,
    @"NSQuitAlwaysKeepsWindows" : @NO,
  }];

  // What an earlier engine kept in the clear, which it keeps off now (ADR
  // 0113, 0115), goes with the erase below.
  LogFailures("Could not stage an earlier engine's traces",
              model::StageEngineTraces(data_directory).size());

  // The panic wipe's data goes before anything reads the data directory: the
  // wiped instance started this one, or a wipe was cut short.
  const scoped_refptr<model::VolumeTools> tools = model::VolumeTools::For(
      model::DataVolume(data_directory, cache_directory));
  scoped_refptr<model::LaunchErase> erase = model::PanicWipeAtLaunch(
      data_directory, command_line, *tools, safe_storage);

  // The first launch after the Electron app brings its data over, from a
  // copy, before anything here reads the data directory. Under the guard,
  // only from the temporary folder.
  if (const std::optional<model::ElectronSource> electron =
          ElectronSourceFor(application_support);
      electron && InTemporaryFolder(electron->directory)) {
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
        DataVault::WatchdogPath());
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
  AppLock::Loaded loaded = AppLock::Load(data_directory);
  AppLock::LaunchSecret secret;
  if (data_lock && !loaded.unreadable && !loaded.key_lost) {
    secret.secret = safe_storage.DataSecret(/*makes_one=*/!loaded.state.key);
  }

  g_launched = new Launched{.data_directory = data_directory,
                            .cache_directory = cache_directory,
                            .tools = tools,
                            .data_lock = std::move(data_lock),
                            .attach = std::move(attach),
                            .erase = std::move(erase),
                            .loaded = std::move(loaded),
                            .secret = std::move(secret)};
  return std::nullopt;
}

void OpenData() {
  CHECK(g_launched);
  CHECK(!g_data);
  const std::unique_ptr<Launched> launched(std::exchange(g_launched, nullptr));
  g_data = new PaneData(std::move(*launched));
  g_data->Start();
}

base::DictValue DataState() {
  return g_data ? g_data->State() : base::DictValue();
}

void DataDidStop() {
  base::FilePath data_directory;

  // The engine stopped before the browser started. With the thread pool
  // started, what the launch attached detaches as the vault's exit detaches
  // it. Without it (another Pane took this launch through the engine's
  // singleton, which Chromium settles before it starts the pool), the vault
  // can't be made: the launch attached nothing then, since that Pane holds
  // the data's lock, and an attach of some other early exit is the watchdog's
  // to detach as the process ends, as after a crash.
  if (g_launched) {
    const std::unique_ptr<Launched> launched(
        std::exchange(g_launched, nullptr));
    data_directory = launched->data_directory;
    if (base::ThreadPoolInstance::Get()) {
      g_data = new PaneData(std::move(*launched));
    }
  }

  if (g_data) {
    const std::unique_ptr<PaneData> data(std::exchange(g_data, nullptr));
    data_directory = data->data_directory();
    // The image detaches, mounted or still locked, the 5 s an attach or a
    // lock's detach still under way may take waited for first (DataVault).
    // Nothing of Chromium's is left to fire meanwhile: the wait spins no run
    // loop, where the Swift's spun the main one, and cancelled Chromium's
    // timers first.
    data->EngineDidStop();
  }

  // Chromium wrote it as it quit: the profile used last, and when each was
  // (LocalState).
  if (!data_directory.empty() &&
      !model::StartOnDefaultProfile(EngineDirectory(data_directory))) {
    LOG(ERROR) << "Could not point Local State at the Default profile";
  }
}

}  // namespace pane
