// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "pane/model/app_settings.h"
#include "pane/model/persisted_file.h"
#include "pane/model/profile.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's PersistenceTests: the settings cases the Electron
// app's settings-store.test.ts held, and round trips through its formats.

class Persistence : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(directory_.CreateUniqueTempDir()); }

  void Write(const std::string& name, const std::string& text) {
    ASSERT_TRUE(base::WriteFile(directory_.GetPath().AppendASCII(name), text));
  }

  PersistedFile File(std::string_view name) const {
    return PersistedFile(directory_.GetPath(), name);
  }

  std::optional<AppSettings> LoadSettings() const {
    const std::optional<base::Value> state = File("settings").Load().value();
    return state ? SettingsFromState(*state) : std::nullopt;
  }

  std::optional<std::vector<BrowserProfile>> LoadProfiles() const {
    const std::optional<base::Value> state = File("profiles").Load().value();
    return state ? ProfilesFromState(*state) : std::nullopt;
  }

  std::string Read(std::string_view name) const {
    std::string text;
    base::ReadFileToString(
        directory_.GetPath().AppendASCII(std::string(name) + ".json"), &text);
    return text;
  }

 private:
  base::ScopedTempDir directory_;
};

using SettingsPersistenceTest = Persistence;
using ProfilesPersistenceTest = Persistence;

// Fills defaults for fields a persisted settings.json predates.
TEST_F(SettingsPersistenceTest, FillsDefaults) {
  Write(
      "settings.json",
      R"({"state":{"settings":{"chromiumPath":"/Applications/Chromium.app"}},"version":0})");

  AppSettings expected;
  expected.chromium_path = "/Applications/Chromium.app";
  EXPECT_EQ(LoadSettings(), expected);
}

// Restores a persisted theme.
TEST_F(SettingsPersistenceTest, RestoresTheme) {
  Write(
      "settings.json",
      R"({"state":{"settings":{"chromiumPath":"","theme":"dark"}},"version":0})");

  EXPECT_EQ(LoadSettings()->theme, Theme::kDark);
}

// Restores a persisted app icon, automatic by default.
TEST_F(SettingsPersistenceTest, RestoresAppIcon) {
  EXPECT_EQ(AppSettings().app_icon, AppIcon::kAutomatic);

  Write("settings.json",
        R"({"state":{"settings":{"appIcon":"dusk"}},"version":0})");
  EXPECT_EQ(LoadSettings()->app_icon, AppIcon::kDusk);

  AppSettings settings;
  settings.app_icon = AppIcon::kMoonrise;
  ASSERT_TRUE(File("settings").Save(SettingsState(settings)));
  EXPECT_EQ(LoadSettings()->app_icon, AppIcon::kMoonrise);
}

// Shows Day under a light appearance and Night under a dark one when
// automatic.
TEST_F(SettingsPersistenceTest, AutomaticAppIcon) {
  EXPECT_EQ(Resolved(AppIcon::kAutomatic, false), AppIcon::kDay);
  EXPECT_EQ(Resolved(AppIcon::kAutomatic, true), AppIcon::kNight);
  EXPECT_EQ(Resolved(AppIcon::kDusk, false), AppIcon::kDusk);
  EXPECT_EQ(Resolved(AppIcon::kDay, true), AppIcon::kDay);

  // Every other choice is its own window, whatever the appearance.
  for (int icon = static_cast<int>(AppIcon::kDay);
       icon <= static_cast<int>(AppIcon::kMoonrise); ++icon) {
    const auto value = static_cast<AppIcon>(icon);
    EXPECT_EQ(Resolved(value, true), value);
    EXPECT_EQ(Resolved(value, false), value);
  }
}

// Locks after 15 minutes without use by default, and keeps Never.
TEST_F(SettingsPersistenceTest, LockAfter) {
  // A settings.json from before ADR 0116.
  Write("settings.json",
        R"({"state":{"settings":{"theme":"dark"}},"version":0})");
  EXPECT_EQ(LoadSettings()->lock_after_minutes, 15);

  AppSettings settings;
  settings.lock_after_minutes = std::nullopt;
  ASSERT_TRUE(File("settings").Save(SettingsState(settings)));
  EXPECT_EQ(LoadSettings()->lock_after_minutes, std::nullopt);

  settings.lock_after_minutes = 1;
  ASSERT_TRUE(File("settings").Save(SettingsState(settings)));
  EXPECT_EQ(LoadSettings()->lock_after_minutes, 1);
}

// Keeps an explicit null as off.
TEST_F(SettingsPersistenceTest, ExplicitNullIsOff) {
  Write("settings.json",
        R"({"state":{"settings":{"tabSleepAfterMinutes":null}},"version":0})");

  const std::optional<AppSettings> settings = LoadSettings();
  EXPECT_EQ(settings->tab_sleep_after_minutes, std::nullopt);
  EXPECT_EQ(settings->profile_unload_after_minutes, 30);
}

// Keeps the sidebar's width, within its range.
TEST_F(SettingsPersistenceTest, SidebarWidth) {
  EXPECT_EQ(AppSettings().sidebar_width, 260);

  AppSettings settings;
  settings.sidebar_width = 312;
  ASSERT_TRUE(File("settings").Save(SettingsState(settings)));
  EXPECT_EQ(LoadSettings()->sidebar_width, 312);

  Write("settings.json",
        R"({"state":{"settings":{"sidebarWidth":900}},"version":0})");
  EXPECT_EQ(LoadSettings()->sidebar_width, 400);
  EXPECT_EQ(ClampedSidebarWidth(120), 200);
}

// The shape the Electron app writes: defaults absent from older files,
// explicit nulls, tabs without isLoaded.
constexpr char kElectronProfiles[] =
    R"({"state":{"profiles":[{"name":"Work","group":null,"fingerprint":{"userAgent":"UA","platform":"MACOS","screen":{"width":1512,"height":982},"language":"en-US","languages":["en-US","en"],"timezone":"America/Sao_Paulo","webgl":null,"hardwareConcurrency":8,"deviceMemory":8,"maxTouchPoints":0},"proxy":{"proxyType":"SOCKS5","host":"127.0.0.1","port":1080,"username":"u","password":null},"id":"p1","tabs":[{"id":"t1","url":"https://example.com/","title":"Example Domain"}],"createdAt":"2026-01-01T00:00:00.000Z","updatedAt":"2026-01-02T00:00:00.000Z"}]},"version":0})";

BrowserProfile Work() {
  BrowserProfile profile;
  profile.id = "p1";
  profile.name = "Work";
  profile.created_at = "a";
  profile.updated_at = "b";
  return profile;
}

// Reads the Electron app's profiles.json with its defaults.
TEST_F(ProfilesPersistenceTest, ReadsElectronFile) {
  Write("profiles.json", kElectronProfiles);

  const std::optional<std::vector<BrowserProfile>> profiles = LoadProfiles();
  ASSERT_TRUE(profiles && !profiles->empty());
  const BrowserProfile& profile = profiles->front();

  EXPECT_EQ(profile.color, ProfileColor::kBlue);
  EXPECT_FALSE(profile.keep_loaded);
  EXPECT_TRUE(profile.saves_history);
  EXPECT_FALSE(profile.clears_history);
  EXPECT_EQ(profile.group, std::nullopt);
  EXPECT_EQ(profile.tabs, (std::vector<Tab>{{.id = "t1",
                                             .url = "https://example.com/",
                                             .title = "Example Domain"}}));
  ASSERT_TRUE(profile.fingerprint);
  EXPECT_EQ(profile.fingerprint->screen.color_depth, 24);
  // Its zone was a preset's, under the older key: it follows the computer.
  EXPECT_EQ(profile.fingerprint->timezone, "");
  EXPECT_TRUE(profile.fingerprint->canvas_noise);
  EXPECT_TRUE(profile.fingerprint->audio_noise);
  ProxyConfig proxy;
  proxy.type = ProxyType::kSocks5;
  proxy.host = "127.0.0.1";
  proxy.port = 1080;
  proxy.username = "u";
  EXPECT_EQ(profile.proxy, proxy);
}

// Round-trips through save and load.
TEST_F(ProfilesPersistenceTest, RoundTrips) {
  BrowserProfile profile = Work();
  profile.color = ProfileColor::kViolet;
  profile.group = "Clients";
  profile.keep_loaded = true;
  profile.saves_history = false;
  profile.tabs = {{.id = "t1", .url = "https://example.com/"}};
  const std::vector<BrowserProfile> state = {profile};

  ASSERT_TRUE(File("profiles").Save(ProfilesState(state)));

  EXPECT_EQ(LoadProfiles(), state);
}

// A zone chosen outlives a quit, and an older Pane still finds the key it
// requires.
TEST_F(ProfilesPersistenceTest, ChosenTimeZone) {
  BrowserProfile profile = Work();
  profile.fingerprint.emplace();
  profile.fingerprint->user_agent = "UA";
  profile.fingerprint->platform = Platform::kMacOS;
  profile.fingerprint->screen = {.width = 1512, .height = 982};
  profile.fingerprint->language = "en-US";
  profile.fingerprint->languages = {"en-US", "en"};
  profile.fingerprint->timezone = "Asia/Tokyo";

  ASSERT_TRUE(File("profiles").Save(ProfilesState({profile})));

  EXPECT_EQ(LoadProfiles()->front().fingerprint->timezone, "Asia/Tokyo");
  const std::string json = Read("profiles");
  EXPECT_NE(json.find(R"("timeZone":"Asia/Tokyo")"), std::string::npos);
  EXPECT_NE(json.find(R"("timezone":"Asia/Tokyo")"), std::string::npos);
}

// A clear still owed outlives a quit.
TEST_F(ProfilesPersistenceTest, ClearOwedPersists) {
  BrowserProfile profile = Work();
  profile.SetSavesHistory(false);

  ASSERT_TRUE(File("profiles").Save(ProfilesState({profile})));

  EXPECT_TRUE(LoadProfiles()->front().clears_history);
}

// A file from before the owed clear's key owes one, on a profile that is off.
TEST_F(ProfilesPersistenceTest, OlderFileOwesAClear) {
  const auto decoded = [](bool saves, std::optional<bool> owed) {
    BrowserProfile profile = Work();
    profile.saves_history = saves;
    profile.clears_history = owed.value_or(false);
    base::Value value = profile.ToValue();
    if (!owed) {
      value.GetDict().Remove("historyClearOwed");
    }
    return BrowserProfile::FromValue(value)->clears_history;
  };

  EXPECT_TRUE(decoded(false, std::nullopt));
  EXPECT_FALSE(decoded(true, std::nullopt));
  EXPECT_FALSE(decoded(false, false));
  EXPECT_TRUE(decoded(false, true));
}

// Writes explicit nulls, as the Electron app does.
TEST_F(ProfilesPersistenceTest, WritesNulls) {
  ASSERT_TRUE(File("profiles").Save(ProfilesState({Work()})));

  const std::string json = Read("profiles");
  EXPECT_NE(json.find(R"("group":null)"), std::string::npos);
  EXPECT_NE(json.find(R"("proxy":null)"), std::string::npos);
  EXPECT_NE(json.find(R"("fingerprint":null)"), std::string::npos);
  EXPECT_NE(json.find(R"("version":0)"), std::string::npos);
}

// Reports a missing file as nothing.
TEST_F(ProfilesPersistenceTest, MissingFile) {
  EXPECT_EQ(File("profiles").Load().value(), std::nullopt);
}

}  // namespace
}  // namespace pane::model
