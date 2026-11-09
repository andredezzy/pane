// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/electron_migration.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "pane/model/app_settings.h"
#include "pane/model/mac/file_operations.h"
#include "pane/model/persisted_file.h"
#include "pane/model/profile.h"
#include "pane/model/security.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's ElectronMigrationTests, case by case.

constexpr char kId[] = "4F1C2A7E-0000-4000-8000-00000000000A";

using Kind = ElectronMigrationOutcome::Kind;

ElectronMigrationOutcome Outcome(Kind kind, int profiles = 0) {
  return {.kind = kind, .profiles = profiles};
}

// The outcome, or the test's failure with the error.
std::optional<ElectronMigrationOutcome> Migrate(const base::FilePath& source,
                                                const base::FilePath& data) {
  base::expected<ElectronMigrationOutcome, std::string> outcome =
      MigrateElectronData(source, data);
  if (!outcome.has_value()) {
    ADD_FAILURE() << outcome.error();
    return std::nullopt;
  }
  return *outcome;
}

std::optional<base::Value> StateOf(const base::FilePath& data,
                                   std::string_view name) {
  base::expected<std::optional<base::Value>, FileError> state =
      PersistedFile(data, name).Load();
  return state.has_value() ? std::move(*state) : std::nullopt;
}

std::optional<std::vector<BrowserProfile>> LoadProfiles(
    const base::FilePath& data) {
  const std::optional<base::Value> state = StateOf(data, "profiles");
  return state ? ProfilesFromState(*state) : std::nullopt;
}

std::optional<AppSettings> LoadSettings(const base::FilePath& data) {
  const std::optional<base::Value> state = StateOf(data, "settings");
  return state ? SettingsFromState(*state) : std::nullopt;
}

std::optional<SecurityState> LoadSecurity(const base::FilePath& data) {
  const std::optional<base::Value> state = StateOf(data, "security");
  return state ? SecurityState::FromValue(*state) : std::nullopt;
}

std::optional<MigrationRecord> LoadRecord(const base::FilePath& data) {
  const std::optional<base::Value> state = StateOf(data, kMigrationRecordName);
  return state ? MigrationRecord::FromValue(*state) : std::nullopt;
}

std::string ReadText(const base::FilePath& path) {
  std::string text;
  base::ReadFileToString(path, &text);
  return text;
}

// Every path under `root`, with a file's bytes.
std::map<std::string, std::optional<std::string>> Snapshot(
    const base::FilePath& root) {
  std::map<std::string, std::optional<std::string>> files;
  base::FileEnumerator entries(root, /*recursive=*/true,
                               base::FileEnumerator::FILES |
                                   base::FileEnumerator::DIRECTORIES |
                                   base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath entry = entries.Next(); !entry.empty();
       entry = entries.Next()) {
    std::string bytes;
    files[entry.value()] = !entries.GetInfo().IsDirectory() &&
                                   base::ReadFileToString(entry, &bytes)
                               ? std::make_optional(bytes)
                               : std::nullopt;
  }
  return files;
}

class ElectronMigrationTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(directory_.CreateUniqueTempDir()); }

  // A data directory of the native app's, not made yet.
  base::FilePath NativeData() const {
    return directory_.GetPath().Append("native");
  }

  // An Electron data directory as the app leaves it: its JSON files, a
  // partition per profile (lowercased), and an extension.
  base::FilePath ElectronData() const {
    const base::FilePath root = directory_.GetPath().Append("electron");
    const base::FilePath partition =
        root.Append("Partitions")
            .Append(base::StrCat({"profile-", base::ToLowerASCII(kId)}));
    const base::FilePath extension =
        root.Append("Extensions")
            .Append("ddkjiahejlhfcafbddmgiahcphecmpfh")
            .Append("2025.1.1_0");

    for (const base::FilePath& folder :
         {partition.Append("Local Storage").Append("leveldb"),
          partition.Append("Cache").Append("Cache_Data"), extension}) {
      EXPECT_TRUE(base::CreateDirectory(folder));
    }

    const std::string profiles = base::StrCat(
        {R"({"state":{"profiles":[{"id":")", kId,
         R"(","name":"Work","color":"ROSE","group":null,"fingerprint":null,"proxy":null,"keepLoaded":true,"tabs":[{"id":"t1","url":"https://example.com/","title":"Example","favicon":"","isLoaded":true}],"createdAt":"2026-01-01T00:00:00.000Z","updatedAt":"2026-01-01T00:00:00.000Z"}]},"version":0})"});
    EXPECT_TRUE(base::WriteFile(root.Append("profiles.json"), profiles));
    EXPECT_TRUE(base::WriteFile(
        root.Append("settings.json"),
        R"({"state":{"settings":{"chromiumPath":"","theme":"dark","tabSleepAfterMinutes":5,"profileUnloadAfterMinutes":null,"cacheBudgetMB":1024}},"version":0})"));
    EXPECT_TRUE(base::WriteFile(
        root.Append("security.json"),
        R"({"state":{"pin":{"hash":"$2b$10$x","length":4},"failedAttempts":0},"version":0})"));
    EXPECT_TRUE(base::WriteFile(partition.Append("Cookies"), "cookies"));
    EXPECT_TRUE(base::WriteFile(partition.Append("Local Storage")
                                    .Append("leveldb")
                                    .Append("000003.log"),
                                "storage"));
    EXPECT_TRUE(base::WriteFile(
        partition.Append("Cache").Append("Cache_Data").Append("data_0"),
        "cache"));
    EXPECT_TRUE(base::WriteFile(partition.Append("Preferences"), "{}"));
    EXPECT_TRUE(base::WriteFile(extension.Append("manifest.json"), "{}"));

    return root;
  }

  // A process that runs from an app bundle with Electron's framework in it,
  // as the Electron app does: a copy of sleep(1), signed ad hoc. With Apple's
  // signature, the copy breaks the launch constraint macOS puts on its own
  // binaries, and macOS files a crash report named Pane for it.
  base::Process FakeElectron(const base::FilePath& root) const {
    const base::FilePath contents = root.Append("Pane.app").Append("Contents");
    const base::FilePath executable = contents.Append("MacOS").Append("Pane");
    EXPECT_TRUE(base::CreateDirectory(executable.DirName()));
    EXPECT_TRUE(CopyItem(base::FilePath("/bin/sleep"), executable).has_value());

    // Signed before the framework is there: codesign checks a bundle's
    // frameworks, and an empty folder is none.
    base::ScopedFD null(HANDLE_EINTR(open("/dev/null", O_WRONLY)));
    base::LaunchOptions quiet;
    quiet.fds_to_remap.emplace_back(null.get(), STDERR_FILENO);
    base::Process codesign = base::LaunchProcess(
        {"/usr/bin/codesign", "--sign", "-", "--force", executable.value()},
        quiet);
    int status = -1;
    EXPECT_TRUE(codesign.IsValid() && codesign.WaitForExit(&status));
    EXPECT_EQ(status, 0);

    EXPECT_TRUE(base::CreateDirectory(
        contents.Append("Frameworks").Append("Electron Framework.framework")));

    return base::LaunchProcess({executable.value(), "30"},
                               base::LaunchOptions());
  }

 private:
  base::ScopedTempDir directory_;
};

// Moves profiles, settings, the PIN, and each profile's data; never touches
// the source.
TEST_F(ElectronMigrationTest, Migrates) {
  const base::FilePath source = ElectronData();
  const base::FilePath data = NativeData();

  const std::map<std::string, std::optional<std::string>> before =
      Snapshot(source);
  EXPECT_EQ(Migrate(source, data), Outcome(Kind::kMigrated, 1));
  EXPECT_EQ(Snapshot(source), before);

  const std::optional<std::vector<BrowserProfile>> profiles =
      LoadProfiles(data);
  ASSERT_TRUE(profiles);
  ASSERT_FALSE(profiles->empty());
  EXPECT_EQ(profiles->front().name, "Work");
  EXPECT_TRUE(profiles->front().keep_loaded);
  ASSERT_FALSE(profiles->front().tabs.empty());
  EXPECT_EQ(profiles->front().tabs.front().url, "https://example.com/");

  const std::optional<AppSettings> settings = LoadSettings(data);
  ASSERT_TRUE(settings);
  EXPECT_EQ(settings->theme, Theme::kDark);
  EXPECT_EQ(settings->profile_unload_after_minutes, std::nullopt);

  const std::optional<SecurityState> security = LoadSecurity(data);
  ASSERT_TRUE(security);
  ASSERT_TRUE(security->pin);
  EXPECT_EQ(security->pin->length, 4);

  const base::FilePath profile = data.Append("Engine").Append(kId);
  EXPECT_TRUE(base::PathExists(profile.Append("Cookies")));
  EXPECT_TRUE(base::PathExists(
      profile.Append("Local Storage").Append("leveldb").Append("000003.log")));
  EXPECT_FALSE(base::PathExists(profile.Append("Cache")));
  EXPECT_FALSE(base::PathExists(profile.Append("Preferences")));
  EXPECT_FALSE(base::PathExists(data.Append("Migration")));

  // The Electron app's extensions stay behind: none lands in a profile.
  const std::optional<MigrationRecord> record = LoadRecord(data);
  ASSERT_TRUE(record);
  EXPECT_EQ(record->profiles, std::vector<std::string>{kId});
  EXPECT_FALSE(base::PathExists(profile.Append("Extensions")));

  EXPECT_EQ(Migrate(source, data), Outcome(Kind::kNotNeeded));
}

// Waits while the Electron app runs, and does nothing without it.
TEST_F(ElectronMigrationTest, Preconditions) {
  const base::FilePath source = ElectronData();
  const base::FilePath data = NativeData();
  base::Process electron = FakeElectron(source.Append("bundle"));
  ASSERT_TRUE(electron.IsValid());

  ASSERT_TRUE(base::CreateSymbolicLink(
      base::FilePath(
          base::StrCat({"host-", base::NumberToString(electron.Pid())})),
      source.Append("SingletonLock")));

  EXPECT_EQ(Migrate(source, data), Outcome(Kind::kSourceInUse));
  EXPECT_FALSE(base::PathExists(data.Append("profiles.json")));
  EXPECT_EQ(Migrate(source.Append("missing"), data), Outcome(Kind::kNoSource));

  EXPECT_TRUE(electron.Terminate(0, /*wait=*/true));
}

// A stale lock whose pid is now another process does not hold it back.
TEST_F(ElectronMigrationTest, StaleLock) {
  const base::FilePath source = ElectronData();
  const base::FilePath data = NativeData();

  // This test's own process: alive, and not an Electron app.
  ASSERT_TRUE(base::CreateSymbolicLink(
      base::FilePath(base::StrCat(
          {"host-", base::NumberToString(base::GetCurrentProcId())})),
      source.Append("SingletonLock")));

  EXPECT_EQ(Migrate(source, data), Outcome(Kind::kMigrated, 1));
}

// A launch that made nothing, or one cut short, still migrates.
TEST_F(ElectronMigrationTest, NotCommitted) {
  const base::FilePath source = ElectronData();
  const base::FilePath data = NativeData();

  // What a launch beside the running Electron app saved (no profiles), and
  // what a migration killed before profiles.json moved left.
  ASSERT_TRUE(PersistedFile(data, "profiles").Save(ProfilesState({})));
  ASSERT_TRUE(
      PersistedFile(data, "settings").Save(SettingsState(AppSettings())));
  ASSERT_TRUE(PersistedFile(data, "security").Save(SecurityState().ToValue()));

  EXPECT_EQ(Migrate(source, data), Outcome(Kind::kMigrated, 1));

  const std::optional<AppSettings> settings = LoadSettings(data);
  ASSERT_TRUE(settings);
  EXPECT_EQ(settings->theme, Theme::kDark);

  const std::optional<SecurityState> security = LoadSecurity(data);
  ASSERT_TRUE(security);
  ASSERT_TRUE(security->pin);
  EXPECT_EQ(security->pin->length, 4);

  const std::optional<std::vector<BrowserProfile>> profiles =
      LoadProfiles(data);
  ASSERT_TRUE(profiles);
  ASSERT_EQ(profiles->size(), 1u);
  EXPECT_EQ(profiles->front().name, "Work");
}

// A record keeps whether the cookie key still waits; an older one reads as
// not.
TEST_F(ElectronMigrationTest, RecordCookieKey) {
  const base::FilePath data = NativeData();
  const PersistedFile file(data, kMigrationRecordName);

  ASSERT_TRUE(file.Save(MigrationRecord("/old", "2026-10-27T00:00:00Z", {"a"},
                                        /*cookie_key_pending=*/true)
                            .ToValue()));
  ASSERT_TRUE(LoadRecord(data));
  EXPECT_EQ(LoadRecord(data)->cookie_key_pending, true);

  ASSERT_TRUE(base::WriteFile(
      file.path(),
      R"({"state":{"source":"/old","migratedAt":"2026-10-26T00:00:00Z","profiles":["a"],"pendingExtensions":{}},"version":0})"));
  ASSERT_TRUE(LoadRecord(data));
  EXPECT_EQ(LoadRecord(data)->cookie_key_pending, std::nullopt);
}

// Reads the Electron app's folder used last, named after its package.
TEST_F(ElectronMigrationTest, DefaultSource) {
  const base::FilePath support = NativeData().DirName().Append("support");

  // Neither folder: the current package's.
  ElectronSource source = DefaultElectronSource(support);
  EXPECT_EQ(source.directory.BaseName().value(), "veil");
  EXPECT_EQ(source.KeychainService(), "@pane/veil Safe Storage");

  // 2026-05-01, a Friday, and 2026-05-05, a Tuesday.
  const std::array<std::pair<std::string_view, base::Time::Exploded>, 2> used =
      {{
          {"@pane/veil",
           {.year = 2026, .month = 5, .day_of_week = 5, .day_of_month = 1}},
          {"@pane/desktop",
           {.year = 2026, .month = 5, .day_of_week = 2, .day_of_month = 5}},
      }};
  for (const auto& [name, exploded] : used) {
    const base::FilePath profiles =
        support.Append(name).Append("profiles.json");
    ASSERT_TRUE(base::CreateDirectory(profiles.DirName()));
    ASSERT_TRUE(base::WriteFile(profiles, "{}"));
    base::Time date;
    ASSERT_TRUE(base::Time::FromUTCExploded(exploded, &date));
    ASSERT_TRUE(base::TouchFile(profiles, date, date));
  }

  // A build from before the rename, used last.
  source = DefaultElectronSource(support);
  EXPECT_EQ(source.app_name, "@pane/desktop");
  EXPECT_EQ(source.directory, support.Append("@pane/desktop"));
  EXPECT_EQ(source.KeychainService(), "@pane/desktop Safe Storage");

  EXPECT_EQ(ElectronSource{.directory = support}.KeychainService(),
            std::nullopt);
}

// The native app's own profiles are never replaced.
TEST_F(ElectronMigrationTest, ExistingData) {
  const base::FilePath source = ElectronData();
  const base::FilePath data = NativeData();

  BrowserProfile own;
  own.id = "own";
  own.name = "Mine";
  own.created_at = "2026-10-27T00:00:00Z";
  own.updated_at = "2026-10-27T00:00:00Z";
  ASSERT_TRUE(PersistedFile(data, "profiles").Save(ProfilesState({own})));

  EXPECT_EQ(Migrate(source, data), Outcome(Kind::kNotNeeded));

  // Nor is a file it cannot read.
  ASSERT_TRUE(base::WriteFile(data.Append("profiles.json"), "{"));

  EXPECT_EQ(Migrate(source, data), Outcome(Kind::kNotNeeded));
}

// A record from before extensions stayed behind is written again without
// them.
TEST_F(ElectronMigrationTest, DropsPendingExtensions) {
  const base::FilePath data = NativeData();
  ASSERT_TRUE(base::CreateDirectory(data));
  const base::FilePath record_path = data.Append("migration.json");
  ASSERT_TRUE(base::WriteFile(
      record_path,
      R"({"state":{"source":"/x/@pane/veil","migratedAt":"2026-10-28T00:00:00Z","profiles":["a"],"pendingExtensions":{"a":["cjpalhdlnbpafiamejdnhcphjbkeiagm"]}},"version":0})"));

  EXPECT_TRUE(DropPendingExtensions(data));
  const std::string written = ReadText(record_path);
  EXPECT_TRUE(written.find("pendingExtensions") == std::string::npos &&
              written.find("cjpalhdlnbpafiamejdnhcphjbkeiagm") ==
                  std::string::npos);

  const std::optional<MigrationRecord> record = LoadRecord(data);
  EXPECT_TRUE(record && record->profiles == std::vector<std::string>{"a"} &&
              record->source == "/x/@pane/veil");

  // A record without them is left as it is.
  const std::string before = ReadText(record_path);
  EXPECT_TRUE(DropPendingExtensions(data));
  EXPECT_EQ(ReadText(record_path), before);
}

// Not in PaneKit: a profiles.json naming a profile outside its folders
// ("..", which as a folder of Engine names the data root) stops the migration
// before it copies or deletes anything.
TEST_F(ElectronMigrationTest, OutsideItsFolders) {
  const base::FilePath source = ElectronData();
  std::string profiles = ReadText(source.Append("profiles.json"));
  base::ReplaceSubstringsAfterOffset(&profiles, 0, kId, "..");
  ASSERT_TRUE(base::WriteFile(source.Append("profiles.json"), profiles));
  const base::FilePath data = NativeData();
  ASSERT_TRUE(base::CreateDirectory(data));
  ASSERT_TRUE(base::WriteFile(data.Append("kept"), "kept"));
  const auto before = Snapshot(data);
  const auto source_before = Snapshot(source);

  EXPECT_FALSE(MigrateElectronData(source, data).has_value());
  EXPECT_EQ(Snapshot(data), before);
  EXPECT_EQ(Snapshot(source), source_before);
}

}  // namespace
}  // namespace pane::model
