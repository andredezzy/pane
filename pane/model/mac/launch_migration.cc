// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/launch_migration.h"

#include <Security/Security.h>

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "pane/model/mac/launch_alert.h"
#include "pane/model/persisted_file.h"

namespace pane::model {

namespace {

using Kind = ElectronMigrationOutcome::Kind;
using Outcome = base::expected<ElectronMigrationOutcome, std::string>;

// For the log: what came of it, never a path.
const char* Described(Kind kind) {
  switch (kind) {
    case Kind::kNotNeeded:
      return "not needed";
    case Kind::kNoSource:
      return "no source";
    case Kind::kSourceInUse:
      return "source in use";
    case Kind::kMigrated:
      return "migrated";
  }
}

// The copy, on a thread of its own, which the launch joins.
class Copy : public base::PlatformThread::Delegate {
 public:
  Copy(const base::FilePath& source, const base::FilePath& data_directory)
      : source_(source), data_directory_(data_directory) {}
  Copy(const Copy&) = delete;
  Copy& operator=(const Copy&) = delete;
  ~Copy() override = default;

  void ThreadMain() override {
    base::PlatformThread::SetName("PaneMigration");
    outcome_ = MigrateElectronData(source_, data_directory_);
    done_.Signal();
  }

  base::WaitableEvent& done() { return done_; }
  // Once joined.
  Outcome TakeOutcome() { return std::move(*outcome_); }

 private:
  const base::FilePath source_;
  const base::FilePath data_directory_;
  base::WaitableEvent done_;
  std::optional<Outcome> outcome_;
};

// A long copy (gigabytes of site data) shows a note after a second.
Outcome Migrate(const base::FilePath& source,
                const base::FilePath& data_directory,
                LaunchAlert& alert) {
  Copy copy(source, data_directory);
  base::PlatformThreadHandle thread;
  if (!base::PlatformThread::Create(0, &copy, &thread)) {
    return MigrateElectronData(source, data_directory);
  }
  if (!copy.done().TimedWait(base::Seconds(1))) {
    const std::unique_ptr<LaunchAlert::Note> note = alert.ShowNote(
        "Bringing over your profiles",
        "Pane is copying the old Pane’s profiles, tabs, and sign-ins. It "
        "opens once they’re here.");
    copy.done().Wait();
  }
  base::PlatformThread::Join(thread);
  return copy.TakeOutcome();
}

// Hands the Electron app's cookie key to the engine, so the copied cookies
// decrypt. macOS asks the user first; a Deny asks again here, since a Pane
// without the key signs the migrated sites out for good. When the Keychain
// could not ask at all (a locked session), a later launch tries again
// (`retrying`), while the engine has no key of its own: it would have
// encrypted new cookies with it.
void AdoptCookieKey(const ElectronSource& source,
                    const base::FilePath& data_directory,
                    SafeStorage safe_storage,
                    LaunchAlert& alert,
                    bool retrying) {
  const std::optional<std::string> service = source.KeychainService();
  if (!source.app_name || !service) {
    return;
  }
  const PersistedFile file(data_directory, kMigrationRecordName);
  const base::expected<std::optional<base::Value>, FileError> loaded =
      file.Load();
  std::optional<MigrationRecord> record =
      loaded.has_value() && *loaded ? MigrationRecord::FromValue(**loaded)
                                    : std::nullopt;
  if (!record || (retrying && !(record->cookie_key_pending == true &&
                                !safe_storage.HasEngineKey()))) {
    return;
  }

  OSStatus status = safe_storage.AdoptKey(*service, *source.app_name);
  while (status == errSecUserCanceled || status == errSecAuthFailed) {
    if (alert.Show("Keep your sign-ins?",
                   "Pane needs the old Pane’s key in your keychain to keep "
                   "the sites you were signed in to. Without it they’re "
                   "signed out; nothing else is lost.",
                   "Try Again", "Continue Signed Out",
                   base::RepeatingCallback<bool()>()) !=
        LaunchAlert::Response::kDefaultButton) {
      break;
    }
    status = safe_storage.AdoptKey(*service, *source.app_name);
  }
  VLOG(1) << "The Electron app's cookie key: " << status;

  // Adopted, missing, or declined: done. Anything else could not ask.
  record->cookie_key_pending =
      status != errSecSuccess && status != errSecItemNotFound &&
      status != errSecUserCanceled && status != errSecAuthFailed;
  if (!file.Save(record->ToValue())) {
    LOG(ERROR) << "Could not save migration.json";
  }
}

}  // namespace

std::optional<int> RunLaunchMigration(const ElectronSource& source,
                                      const base::FilePath& data_directory,
                                      SafeStorage safe_storage,
                                      LaunchAlert& alert) {
  while (true) {
    const base::TimeTicks started = base::TimeTicks::Now();
    const Outcome outcome = Migrate(source.directory, data_directory, alert);
    if (!outcome.has_value()) {
      // Nothing committed (profiles.json moves last), so the next launch tries
      // again, unless the user makes profiles first. What failed names a
      // path: not for the log.
      LOG(ERROR) << "The migration from the Electron app failed";
      return std::nullopt;
    }
    VLOG(1) << "The migration from the Electron app: "
            << Described(outcome->kind) << " ("
            << (base::TimeTicks::Now() - started).InSecondsF() << " s)";

    switch (outcome->kind) {
      case Kind::kSourceInUse:
        // Its files may be mid-write: Pane waits for it to quit, or quits
        // itself, having written nothing.
        if (alert.Show("Quit the old Pane to continue",
                       "Pane brings over the old Pane’s profiles, tabs, and "
                       "sign-ins, and needs it closed to copy them. Quit it, "
                       "and Pane goes on by itself.",
                       "Cancel", std::nullopt,
                       base::BindRepeating(
                           [](const base::FilePath& source) {
                             return !IsElectronAppRunning(source);
                           },
                           source.directory)) !=
            LaunchAlert::Response::kDismissed) {
          return 0;
        }
        break;

      case Kind::kMigrated:
        AdoptCookieKey(source, data_directory, safe_storage, alert,
                       /*retrying=*/false);
        return std::nullopt;

      case Kind::kNotNeeded:
        AdoptCookieKey(source, data_directory, safe_storage, alert,
                       /*retrying=*/true);
        return std::nullopt;

      case Kind::kNoSource:
        return std::nullopt;
    }
  }
}

}  // namespace pane::model
