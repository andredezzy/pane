// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_ELECTRON_MIGRATION_H_
#define PANE_MODEL_MAC_ELECTRON_MIGRATION_H_

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"

namespace pane::model {

// The first launch after the Electron app (ADR 0043): its profiles, tabs,
// settings, PIN, and each profile's site data (cookies, storage) move into
// the native app. Its extensions do not: it installed each once, for every
// profile, and a native profile has only the extensions added to it. It
// always reads a copy of the Electron app's data, never the data itself, and
// never writes there (only the panic wipe erases it: ElectronData). It runs
// until it has migrated: a launch that found the Electron app running, or
// failed, wrote nothing that stops the next one.

// What a migration did, kept in the data directory as migration.json so it
// runs once. The panic wipe leaves it, so a wiped Pane never brings the
// Electron app's data back. A record from before extensions stayed behind
// still lists them as pendingExtensions, which nothing reads.
struct MigrationRecord {
  MigrationRecord();
  MigrationRecord(std::string source,
                  std::string migrated_at,
                  std::vector<std::string> profiles,
                  std::optional<bool> cookie_key_pending = std::nullopt);
  MigrationRecord(const MigrationRecord&);
  MigrationRecord& operator=(const MigrationRecord&);
  ~MigrationRecord();

  // nullopt for a record that doesn't decode.
  static std::optional<MigrationRecord> FromValue(const base::Value& value);
  // Without cookieKeyPending when it is nullopt, as Swift's coding leaves it.
  base::Value ToValue() const;

  friend bool operator==(const MigrationRecord&,
                         const MigrationRecord&) = default;

  std::string source;
  std::string migrated_at;
  std::vector<std::string> profiles;
  // The Electron app's cookie key still waits to go to the engine: the
  // Keychain could not ask the user (a locked session, say). nullopt in a
  // record from before this was kept.
  std::optional<bool> cookie_key_pending;
};

inline constexpr char kMigrationRecordName[] = "migration";

struct ElectronMigrationOutcome {
  enum class Kind {
    // The native app has profiles of its own, or migrated before.
    kNotNeeded,
    // No Electron app data where it lives.
    kNoSource,
    // The Electron app is running: its files may be mid-write.
    kSourceInUse,
    kMigrated,
  };

  friend bool operator==(const ElectronMigrationOutcome&,
                         const ElectronMigrationOutcome&) = default;

  Kind kind = Kind::kNotNeeded;
  // The profiles migrated.
  int profiles = 0;
};

// Where the Electron app keeps its data, and the name it runs under.
struct ElectronSource {
  friend bool operator==(const ElectronSource&,
                         const ElectronSource&) = default;

  // The item whose key encrypts the Electron app's cookies: "<app name> Safe
  // Storage", with the app name as its account.
  std::optional<std::string> KeychainService() const;

  base::FilePath directory;
  // Names the Electron app's Keychain item; nullopt for a folder named by
  // hand (PANE_ELECTRON_DATA_DIR).
  std::optional<std::string> app_name;
};

// The names the packaged Electron app ran under. apps/veil has no
// productName, so Electron names its data folder and its Keychain item after
// the package: @pane/veil, and @pane/desktop before the package's rename
// (2026-05-05).
inline constexpr std::array<std::string_view, 2> kElectronAppNames = {
    "@pane/veil", "@pane/desktop"};

// The Electron app's data: of its folders, the one used last.
ElectronSource DefaultElectronSource(const base::FilePath& application_support);

// A record from before extensions stayed behind still lists them as
// pendingExtensions, which nothing reads: the extensions each profile had, in
// the clear beside the sealed profiles. Written again without them (ADR
// 0115). False when the record would not read or write.
bool DropPendingExtensions(const base::FilePath& data_directory);

// The migration, from `source` (the Electron app's data) into
// `data_directory`; an error says what would not copy or move.
base::expected<ElectronMigrationOutcome, std::string> MigrateElectronData(
    const base::FilePath& source,
    const base::FilePath& data_directory,
    base::Time now = base::Time::Now());

// Chromium marks a running instance with a SingletonLock link to
// "<host>-<pid>". The Electron app runs while that pid is alive (EPERM:
// alive, another user's) and is an Electron app: a stale lock whose pid came
// back as another process would hold the migration back for good.
bool IsElectronAppRunning(const base::FilePath& source);

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_ELECTRON_MIGRATION_H_
