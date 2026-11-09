// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/electron_migration.h"

#include <errno.h>
#include <libproc.h>
#include <signal.h>

#include <algorithm>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/types/expected_macros.h"
#include "pane/model/mac/file_operations.h"
#include "pane/model/persisted_file.h"
#include "pane/model/profile.h"
#include "pane/model/value_reader.h"

namespace pane::model {

namespace {

// What a profile's partition holds that a browser cache can rebuild: not
// worth copying.
constexpr std::array<std::string_view, 8> kCaches = {
    "Cache",           "Code Cache",    "GPUCache",    "DawnGraphiteCache",
    "DawnWebGPUCache", "GrShaderCache", "ShaderCache", "Crashpad"};

// Files that belong to the Electron session, not to the profile's data.
constexpr std::array<std::string_view, 3> kSessionFiles = {"Preferences",
                                                           "LOCK", "lockfile"};

std::string JsonName(std::string_view name) {
  return base::StrCat({name, ".json"});
}

base::Time LastUsed(const base::FilePath& directory) {
  base::File::Info info;
  return base::GetFileInfo(directory.Append("profiles.json"), &info)
             ? info.last_modified
             : base::Time::Min();
}

// As ISO8601DateFormatter writes it: "2026-10-27T00:00:00Z".
std::string Iso8601(base::Time time) {
  base::Time::Exploded exploded;
  time.UTCExplode(&exploded);
  return base::StringPrintf("%04d-%02d-%02dT%02d:%02d:%02dZ", exploded.year,
                            exploded.month, exploded.day_of_month,
                            exploded.hour, exploded.minute, exploded.second);
}

// Copies a partition's data, leaving out caches and the Electron session's
// own files.
base::expected<void, std::string> CopyData(const base::FilePath& partition,
                                           const base::FilePath& destination) {
  if (!base::CreateDirectory(destination)) {
    return base::unexpected(LastFileError(destination));
  }
  base::FileEnumerator entries(
      partition, /*recursive=*/false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
          base::FileEnumerator::SHOW_SYM_LINKS,
      base::FilePath::StringType(),
      base::FileEnumerator::FolderSearchPolicy::MATCH_ONLY,
      base::FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  for (base::FilePath entry = entries.Next(); !entry.empty();
       entry = entries.Next()) {
    const std::string name = entry.BaseName().value();
    if (std::ranges::contains(kCaches, name) ||
        std::ranges::contains(kSessionFiles, name)) {
      continue;
    }
    RETURN_IF_ERROR(CopyItem(entry, destination.Append(name)));
  }
  if (entries.GetError() != base::File::FILE_OK) {
    return base::unexpected(
        base::StrCat({partition.value(), ": ",
                      base::File::ErrorToString(entries.GetError())}));
  }
  return base::ok();
}

// Profiles of the native app's own, which the migration never replaces. An
// empty list is none: a launch that made nothing still migrates. One that
// cannot be read counts as some.
bool HasProfiles(const base::FilePath& data_directory) {
  const base::expected<std::optional<base::Value>, FileError> state =
      PersistedFile(data_directory, "profiles").Load();
  if (!state.has_value()) {
    return true;
  }
  if (!*state) {
    return false;
  }
  const std::optional<std::vector<BrowserProfile>> profiles =
      ProfilesFromState(**state);
  return !profiles || !profiles->empty();
}

// Whether the process runs from an app bundle with Electron's framework in
// it.
bool IsElectronApp(pid_t pid) {
  std::array<char, PROC_PIDPATHINFO_MAXSIZE> path;
  const int length =
      proc_pidpath(pid, path.data(), static_cast<uint32_t>(path.size()));
  if (length <= 0) {
    return false;
  }
  // <app>/Contents/MacOS/<executable>
  const base::FilePath executable(base::as_string_view(
      base::span(path).first(static_cast<size_t>(length))));
  const base::FilePath contents = executable.DirName().DirName();
  return base::PathExists(
      contents.Append("Frameworks").Append("Electron Framework.framework"));
}

}  // namespace

MigrationRecord::MigrationRecord() = default;

MigrationRecord::MigrationRecord(std::string source,
                                 std::string migrated_at,
                                 std::vector<std::string> profiles,
                                 std::optional<bool> cookie_key_pending)
    : source(std::move(source)),
      migrated_at(std::move(migrated_at)),
      profiles(std::move(profiles)),
      cookie_key_pending(cookie_key_pending) {}

MigrationRecord::MigrationRecord(const MigrationRecord&) = default;
MigrationRecord& MigrationRecord::operator=(const MigrationRecord&) = default;
MigrationRecord::~MigrationRecord() = default;

// static
std::optional<MigrationRecord> MigrationRecord::FromValue(
    const base::Value& value) {
  ValueReader reader(&value);
  MigrationRecord record;
  record.source = reader.String("source");
  record.migrated_at = reader.String("migratedAt");
  record.profiles = reader.Strings("profiles");
  record.cookie_key_pending = reader.OptionalBool("cookieKeyPending");
  if (!reader.ok()) {
    return std::nullopt;
  }
  return record;
}

base::Value MigrationRecord::ToValue() const {
  base::ListValue ids;
  for (const std::string& id : profiles) {
    ids.Append(id);
  }
  base::DictValue dict;
  dict.Set("source", source);
  dict.Set("migratedAt", migrated_at);
  dict.Set("profiles", std::move(ids));
  if (cookie_key_pending) {
    dict.Set("cookieKeyPending", *cookie_key_pending);
  }
  return base::Value(std::move(dict));
}

std::optional<std::string> ElectronSource::KeychainService() const {
  if (!app_name) {
    return std::nullopt;
  }
  return base::StrCat({*app_name, " Safe Storage"});
}

ElectronSource DefaultElectronSource(
    const base::FilePath& application_support) {
  std::vector<ElectronSource> sources;
  for (std::string_view name : kElectronAppNames) {
    sources.push_back({.directory = application_support.Append(name),
                       .app_name = std::string(name)});
  }
  // The first of those used last, as Swift's max(by:) keeps.
  ElectronSource used_last = sources.front();
  for (const ElectronSource& source : sources) {
    if (LastUsed(used_last.directory) < LastUsed(source.directory)) {
      used_last = source;
    }
  }
  return used_last;
}

bool DropPendingExtensions(const base::FilePath& data_directory) {
  const PersistedFile file(data_directory, kMigrationRecordName);
  std::string raw;
  if (!base::ReadFileToString(file.path(), &raw)) {
    return true;
  }
  const std::optional<base::Value> object =
      base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
  const base::DictValue* state = object && object->is_dict()
                                     ? object->GetDict().FindDict("state")
                                     : nullptr;
  if (!state || !state->Find("pendingExtensions")) {
    return true;
  }

  const base::expected<std::optional<base::Value>, FileError> loaded =
      file.Load();
  if (!loaded.has_value()) {
    return false;
  }
  if (!*loaded) {
    return true;
  }
  const std::optional<MigrationRecord> record =
      MigrationRecord::FromValue(**loaded);
  return record && file.Save(record->ToValue());
}

base::expected<ElectronMigrationOutcome, std::string> MigrateElectronData(
    const base::FilePath& source,
    const base::FilePath& data_directory,
    base::Time now) {
  using Kind = ElectronMigrationOutcome::Kind;

  if (base::PathExists(data_directory.Append(JsonName(kMigrationRecordName))) ||
      HasProfiles(data_directory)) {
    return ElectronMigrationOutcome{.kind = Kind::kNotNeeded};
  }
  if (!base::PathExists(source.Append("profiles.json"))) {
    return ElectronMigrationOutcome{.kind = Kind::kNoSource};
  }
  if (IsElectronAppRunning(source)) {
    return ElectronMigrationOutcome{.kind = Kind::kSourceInUse};
  }

  // 1. The copy.
  const base::FilePath staging = data_directory.Append("Migration");
  base::DeletePathRecursively(staging);
  if (!base::CreateDirectory(staging)) {
    return base::unexpected(LastFileError(staging));
  }
  base::ScopedClosureRunner remove_staging(base::BindOnce(
      base::IgnoreResult(&base::DeletePathRecursively), staging));

  for (std::string_view name : {"profiles", "settings", "security"}) {
    const base::FilePath file = source.Append(JsonName(name));
    if (base::PathExists(file)) {
      RETURN_IF_ERROR(CopyItem(file, staging.Append(JsonName(name))));
    }
  }

  const base::expected<std::optional<base::Value>, FileError> state =
      PersistedFile(staging, "profiles").Load();
  if (!state.has_value()) {
    return base::unexpected(
        base::StrCat({source.value(), ": profiles.json does not read"}));
  }
  if (!*state) {
    return ElectronMigrationOutcome{.kind = Kind::kNoSource};
  }
  const std::optional<std::vector<BrowserProfile>> profiles =
      ProfilesFromState(**state);
  if (!profiles) {
    return base::unexpected(base::StrCat(
        {source.value(), ": profiles.json is not the Electron app's"}));
  }
  // An id becomes a folder this deletes and replaces: one that names
  // another place (".." would name the data root) stops the migration
  // before anything is copied or deleted.
  for (const BrowserProfile& profile : *profiles) {
    if (!IsProfileFolderName(profile.id)) {
      return base::unexpected(base::StrCat(
          {source.value(),
           ": profiles.json names a profile outside its folders"}));
    }
  }

  for (const BrowserProfile& profile : *profiles) {
    // Electron names a partition directory in lowercase.
    const base::FilePath partition =
        source.Append("Partitions")
            .Append(base::StrCat({"profile-", base::ToLowerASCII(profile.id)}));
    if (base::PathExists(partition)) {
      RETURN_IF_ERROR(
          CopyData(partition, staging.Append("Partitions").Append(profile.id)));
    }
  }

  // 2. The import, from the copy only.
  const base::FilePath engine = data_directory.Append("Engine");
  if (!base::CreateDirectory(engine)) {
    return base::unexpected(LastFileError(engine));
  }

  for (const BrowserProfile& profile : *profiles) {
    const base::FilePath copied =
        staging.Append("Partitions").Append(profile.id);
    if (base::PathExists(copied)) {
      // A migration cut short left it there; profiles.json, written last,
      // was not.
      const base::FilePath destination = engine.Append(profile.id);
      base::DeletePathRecursively(destination);
      RETURN_IF_ERROR(MoveItem(copied, destination));
    }
  }

  // One left by a migration cut short, or by a launch that made only
  // settings, is replaced. profiles.json goes last: it commits.
  for (std::string_view name : {"settings", "security", "profiles"}) {
    const base::FilePath file = staging.Append(JsonName(name));
    const base::FilePath destination = data_directory.Append(JsonName(name));
    if (base::PathExists(file)) {
      base::DeletePathRecursively(destination);
      RETURN_IF_ERROR(MoveItem(file, destination));
    }
  }

  std::vector<std::string> ids;
  for (const BrowserProfile& profile : *profiles) {
    ids.push_back(profile.id);
  }
  const MigrationRecord record(source.value(), Iso8601(now), std::move(ids));
  if (!PersistedFile(data_directory, kMigrationRecordName)
           .Save(record.ToValue())) {
    return base::unexpected(
        base::StrCat({data_directory.value(), ": migration.json not saved"}));
  }

  return ElectronMigrationOutcome{
      .kind = Kind::kMigrated, .profiles = static_cast<int>(profiles->size())};
}

bool IsElectronAppRunning(const base::FilePath& source) {
  base::FilePath target;
  if (!base::ReadSymbolicLink(source.Append("SingletonLock"), &target)) {
    return false;
  }
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      target.value(), "-", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  int pid = 0;
  if (parts.empty() || !base::StringToInt(parts.back(), &pid)) {
    return false;
  }
  if (kill(pid, 0) != 0 && errno != EPERM) {
    return false;
  }
  return IsElectronApp(pid);
}

}  // namespace pane::model
