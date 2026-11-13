// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/data_launch.h"

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/default_clock.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "pane/browser/profiles.h"
#include "pane/encryption/engine_keys.h"
#include "pane/model/app_lock.h"
#include "pane/model/data_vault.h"
#include "pane/model/local_state.h"
#include "pane/model/model_store.h"
#include "pane/model/pin_entry.h"
#include "pane/model/security.h"
#include "pane/model/test_hooks.h"

namespace pane {

namespace {

using model::AppLock;
using model::DataVault;

base::FilePath EngineDirectory(const base::FilePath& data_directory) {
  return data_directory.Append(FILE_PATH_LITERAL("Engine"));
}

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

// The vault, the lock, and the model's store, from the thread pool's start to
// the engine's stop, and what each tells the app (the Swift's AppDelegate and
// BrowserController's halves of it): the profiles load once the data key
// opens, an earlier Pane's moving in show from the first frame
// (DataVault::ExpectMove), and the lock's close leaves the engine no key.
class PaneData : public AppData,
                 public DataVault::Delegate,
                 public DataVault::Observer,
                 public AppLock::Delegate,
                 public AppLock::Observer {
 public:
  explicit PaneData(LaunchedData launched)
      : data_directory_(launched.data_directory),
        erase_(std::move(launched.erase)),
        has_plain_profiles_(launched.has_plain_profiles),
        vault_(launched.data_directory, std::move(launched.place), this),
        lock_(launched.data_directory,
              std::move(launched.loaded),
              vault_,
              std::move(launched.lock_system),
              this,
              std::move(launched.secret)),
        store_(launched.data_directory,
               std::move(launched.settings),
               base::DefaultClock::GetInstance(),
               base::BindRepeating(&DataVault::FilesKey,
                                   base::Unretained(&vault_)),
               std::move(launched.kept_apart)) {
    // Not when the engine stopped before the browser started (DataDidStop),
    // with nothing to convert.
    if (ProfileManager* manager = g_browser_process
                                      ? g_browser_process->profile_manager()
                                      : nullptr) {
      vault_.set_file_migrator(MakeFileMigrator(manager->user_data_dir()));
      // The vault reaches the data by the data directory as Pane was given
      // it, where Chromium resolved its own: a link in it (/var is
      // /private/var) makes two forms, and the registry refuses a form it
      // doesn't hold (ADR 0121). The vault's markers in a profile's folder
      // are then the profile's, as on a data directory with no link.
      encryption::AddRoots(
          encryption::RootsOf(EngineDirectory(data_directory_)));
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
                         // The browser's Engine keys: none once a lock has
                         // closed the data (ADR 0121).
                         .Set("keys", static_cast<int>(encryption::KeyCount()))
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

LaunchedData* g_launched = nullptr;
PaneData* g_data = nullptr;

}  // namespace

base::FilePath DataDirectory() {
  if (const std::optional<std::string> named =
          model::TestHook("PANE_DATA_DIR")) {
    return base::FilePath::FromUTF8Unsafe(*named);
  }
  return DefaultDataDirectory();
}

LaunchedData::LaunchedData() = default;
LaunchedData::LaunchedData(LaunchedData&&) = default;
LaunchedData& LaunchedData::operator=(LaunchedData&&) = default;
LaunchedData::~LaunchedData() = default;

void KeepLaunchedData(LaunchedData launched) {
  CHECK(!g_launched);
  g_launched = new LaunchedData(std::move(launched));
}

void OpenData() {
  CHECK(g_launched);
  CHECK(!g_data);
  const std::unique_ptr<LaunchedData> launched(
      std::exchange(g_launched, nullptr));
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
  // started, what the launch opened closes as the vault's exit closes it.
  // Without it (another Pane took this launch through the engine's
  // singleton, which Chromium settles before it starts the pool), the vault
  // can't be made: the launch attached nothing then, since that Pane holds
  // the data's lock, and an attach of some other early exit is the Mac's
  // watchdog's to detach as the process ends, as after a crash.
  if (g_launched) {
    const std::unique_ptr<LaunchedData> launched(
        std::exchange(g_launched, nullptr));
    data_directory = launched->data_directory;
    if (base::ThreadPoolInstance::Get()) {
      g_data = new PaneData(std::move(*launched));
    }
  }

  if (g_data) {
    const std::unique_ptr<PaneData> data(std::exchange(g_data, nullptr));
    data_directory = data->data_directory();
    // What is open closes: on the Mac the image detaches, mounted or still
    // locked, the 5 s an attach or a lock's detach still under way may take
    // waited for first (VolumePlace). Nothing of Chromium's is left to fire
    // meanwhile: the wait spins no run loop, where the Swift's spun the main
    // one, and cancelled Chromium's timers first.
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
