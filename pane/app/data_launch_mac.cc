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
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/default_clock.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "pane/app/app_defaults.h"
#include "pane/app/test_copy.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/profiles.h"
#include "pane/encryption/profile_migration.h"
#include "pane/model/app_settings.h"
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
#include "pane/model/mac/proxy_password_store.h"
#include "pane/model/mac/safe_storage.h"
#include "pane/model/model_store.h"
#include "pane/model/pin_entry.h"
#include "pane/model/security.h"
#include "pane/model/test_hooks.h"

namespace pane {

namespace {

using model::AppLock;
using model::DataVault;
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
  // settings.json, read once: the model starts with them.
  model::AppSettings settings;
  // An earlier Pane's plain profile folders wait to move into the volume.
  bool has_plain_profiles = false;
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

// The engine's side of the vault's work on a profile's folder (ADR 0121),
// run on the profile's sequence: the conversion of an earlier Pane's files,
// the restore of its safety copy, and the repair after 0.18.0's migration,
// each with the profile's key registered first, as the Swift's engine ran
// them. Traced as the Swift app's engine traced them, for the scenarios.
DataVault::FileMigrator MakeFileMigrator(const base::FilePath& user_data_dir) {
  DataVault::FileMigrator migrator;
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
        PANE_TRACE("call", "PaneBrowserApplication.repairContext",
                   base::DictValue().Set("identifier", id));
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

// The vault, the lock, and the model's store, from the thread pool's start to
// the engine's stop, and what each tells the app (the Swift's AppDelegate and
// BrowserController's halves of it): the profiles load once the data key
// opens, an earlier Pane's moving in show from the first frame
// (DataVault::ExpectMove), and the lock's detach leaves the engine no key.
class PaneData : public AppData,
                 public DataVault::Delegate,
                 public DataVault::Observer,
                 public AppLock::Delegate,
                 public AppLock::Observer {
 public:
  explicit PaneData(Launched launched)
      : data_directory_(launched.data_directory),
        erase_(std::move(launched.erase)),
        has_plain_profiles_(launched.has_plain_profiles),
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
              std::move(launched.secret)),
        store_(launched.data_directory,
               std::move(launched.settings),
               base::DefaultClock::GetInstance(),
               base::BindRepeating(&DataVault::FilesKey,
                                   base::Unretained(&vault_)),
               KeptApartPasswords(launched.data_directory)) {
    // Not when the engine stopped before the browser started (DataDidStop),
    // with nothing to convert.
    if (ProfileManager* manager = g_browser_process
                                      ? g_browser_process->profile_manager()
                                      : nullptr) {
      vault_.set_file_migrator(MakeFileMigrator(manager->user_data_dir()));
    }
  }
  PaneData(const PaneData&) = delete;
  PaneData& operator=(const PaneData&) = delete;
  ~PaneData() override = default;

  const base::FilePath& data_directory() const { return data_directory_; }

  void EngineDidStop() { vault_.EngineDidStop(); }

  // AppData:
  void Start(Client* client) override {
    client_ = client;
    lock_.AddObserver(this);
    vault_.AddObserver(this);
    // An earlier Pane's profiles move in at once, with no PIN to ask first:
    // the panel shows from the first frame.
    if (!lock_.is_locked() && has_plain_profiles_) {
      vault_.ExpectMove();
    }
    // Without a PIN the data key opens at once, off this sequence: the volume
    // is made the first time, attached, and opened.
    lock_.Start();
  }

  void Stop() override {
    lock_.RemoveObserver(this);
    vault_.RemoveObserver(this);
    client_ = nullptr;
  }

  model::ModelStore& store() override { return store_; }

  bool IsLocked() const override { return lock_.is_locked(); }

  bool IsOpen() const override {
    return vault_.phase() == DataVault::Phase::kOpen;
  }

  bool HasPin() const override { return lock_.state().pin.has_value(); }

  std::optional<model::Key> EngineKey(
      std::string_view profile_id) const override {
    return vault_.EngineKey(profile_id);
  }

  void WhenAttached(base::OnceCallback<void(bool)> body) override {
    vault_.WhenAttached(std::move(body));
  }

  void Prepare(const std::string& profile_id,
               bool saves_history,
               base::OnceCallback<void(bool)> done) override {
    vault_.Prepare(profile_id, saves_history, std::move(done));
  }

  void ProfileDidOpen(const std::string& profile_id) override {
    vault_.ProfileDidOpen(profile_id);
  }

  void RecordDeletion(const std::string& profile_id,
                      base::OnceClosure recorded) override {
    vault_.RecordDeletion(profile_id, std::move(recorded));
  }

  void RemoveDeleted(const std::string& profile_id) override {
    vault_.Remove(profile_id, base::DoNothing());
  }

  void LockScreen() override { lock_.LockNow(); }

  base::OnceCallback<void(bool)> CloseData() override { return vault_.Lock(); }

  const std::optional<model::PinEntry>& PinScreen() const override {
    return lock_.entry();
  }

  int FailedAttempts() const override { return lock_.state().failed_attempts; }

  std::optional<double> DataMoveShare() const override {
    return vault_.share();
  }

  void ShowPinScreen(model::PinEntry::Mode mode) override { lock_.Show(mode); }
  void PinDigit(char16_t digit) override { lock_.Digit(digit); }
  void PinBackspace() override { lock_.Backspace(); }
  void ContinueWithPin() override { lock_.ContinueWithPin(); }
  void DismissPinScreen() override { lock_.Dismiss(); }

  base::DictValue State() const override {
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
  void DataKeyDidOpen(base::OnceClosure loaded) override {
    store_.LoadProfiles(std::move(loaded));
  }

  std::vector<std::string> ProfileIds() override {
    std::vector<std::string> ids;
    for (const model::BrowserProfile& profile : store_.model().profiles()) {
      ids.push_back(profile.id);
    }
    return ids;
  }

  void AccessDidOpen() override {
    if (client_) {
      client_->AccessDidOpen();
    }
  }

  void VolumeWontOpen() override { lock_.VolumeWontOpen(); }

  void QuitToCloseVolume() override { chrome::AttemptExit(); }

  // No process has a profile's file open: each forgets its keys (ADR 0121).
  void VolumeDidDetachAtLock() override {
    DropEngineKeys(
        base::BindOnce([](size_t browser, uint32_t network, uint32_t storage) {
          if (browser + network + storage > 0) {
            LOG(ERROR) << "A process kept its keys through a lock: " << browser
                       << ", " << network << ", " << storage;
          }
        }));
  }

  // DataVault::Observer: the data's move shows on the PIN screen's panel.
  void OnVaultPhaseChanged() override {
    if (client_) {
      client_->SecurityChanged();
    }
  }

  // AppLock::Observer:
  void OnAppLockChanged() override {
    if (client_) {
      client_->SecurityChanged();
    }
  }

  void OnPinShake() override {
    if (client_) {
      client_->PinShook();
    }
  }

  // AppLock::Delegate:
  void DidUnlock() override { AccessDidOpen(); }

  void DidExhaustSettingsAttempts() override {
    if (client_) {
      client_->LockRequested();
    }
  }

 private:
  const base::FilePath data_directory_;
  const scoped_refptr<model::LaunchErase> erase_;
  const bool has_plain_profiles_;
  raw_ptr<Client> client_ = nullptr;
  DataVault vault_;
  AppLock lock_;
  // After the vault, whose files key it reads.
  model::ModelStore store_;
};

PaneData* g_data = nullptr;

}  // namespace

base::FilePath DataDirectory() {
  if (const std::optional<std::string> named = TestHook("PANE_DATA_DIR")) {
    return base::FilePath(*named);
  }
  return DefaultDataDirectory();
}

base::FilePath DefaultDataDirectory() {
  return UserDirectory(base::DIR_APP_DATA).Append(model::kBundleIdentifier);
}

// The order is main.swift's, step by step.
std::optional<int> PrepareData(const base::FilePath& data_directory,
                               const base::CommandLine& command_line,
                               bool test_copy) {
  CHECK(!g_launched);
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
      data_directory, command_line, *tools, safe_storage);

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

  // The model starts with its settings alone (ADR 0113): read here, after the
  // Electron app's were brought over, while the disk may be read.
  model::AppSettings settings = model::ModelStore::LoadSettings(data_directory);

  // An earlier Pane's plain profile folders, which move into the volume
  // behind the panel from the first frame when no PIN is asked first.
  const bool has_plain_profiles =
      model::DataVolume(data_directory, cache_directory).HasPlainProfiles();

  g_launched = new Launched{.data_directory = data_directory,
                            .cache_directory = cache_directory,
                            .tools = tools,
                            .data_lock = std::move(data_lock),
                            .attach = std::move(attach),
                            .erase = std::move(erase),
                            .loaded = std::move(loaded),
                            .secret = std::move(secret),
                            .settings = std::move(settings),
                            .has_plain_profiles = has_plain_profiles};
  return std::nullopt;
}

void OpenData() {
  CHECK(g_launched);
  CHECK(!g_data);
  const std::unique_ptr<Launched> launched(std::exchange(g_launched, nullptr));
  g_data = new PaneData(std::move(*launched));
}

// static
AppData* AppData::Get() {
  return g_data;
}

void DataWillStop() {
  if (g_data) {
    g_data->store().model().SaveNow();
  }
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
