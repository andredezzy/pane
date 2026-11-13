// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/data_key.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/hash.h"
#include "crypto/kdf.h"
#include "pane/model/app_settings.h"
#include "pane/model/host_platform.h"
#include "pane/model/persisted_file.h"
#include "pane/model/profile.h"
#include "pane/model/profile_draft.h"
#include "pane/model/profile_form.h"
#include "pane/model/sealed_file.h"
#include "pane/model/security.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

Key Sevens() {
  Key key;
  key.fill(7);
  return key;
}

base::span<const uint8_t> Bytes(std::string_view text) {
  return base::as_byte_span(text);
}

constexpr char kSecret[] = "engine-safe-storage-secret";

// Ported from PaneKit's DataKeyTests, case by case.

// Files encrypted, a volume changes to a passphrase an earlier Pane doesn't
// derive (ADR 0121), which still derives the one it was made with.
TEST(DataKeyTest, VolumePassphraseMovesOn) {
  const Key key = Sevens();
  EXPECT_NE(VolumePassphrase(key), FormerVolumePassphrase(key));
  EXPECT_EQ(FormerVolumePassphrase(key),
            base::Base64Encode(crypto::kdf::Hkdf<kKeySize>(
                crypto::hash::kSha256, key, {},
                base::as_byte_span(std::string_view("Pane data volume")))));
}

// Each profile's engine key (ADR 0121): its own, and neither the files' key
// nor the volume's passphrase. The vector is what tests/lib/engine-key.ts
// derives, to look for the key on disk.
TEST(DataKeyTest, EngineKeyIsPerProfile) {
  const Key key = Sevens();
  const Key one = EngineFilesKey(key, "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02");
  const Key two = EngineFilesKey(key, "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f03");
  EXPECT_NE(one, two);
  EXPECT_NE(one, FilesKey(key));
  EXPECT_EQ(base::HexEncodeLower(one),
            "095fb86a06d3e7f7b357b5166b2f83a7e9764eda45d155dcdad6c102bd354fc2");
}

// Sealed without a PIN, the key opens with the secret alone.
TEST(DataKeyTest, WithoutPin) {
  const Key key = MakeDataKey();
  const SealedKey sealed = SealDataKey(key, Bytes(kSecret), std::nullopt);

  EXPECT_FALSE(sealed.pin);
  EXPECT_EQ(OpenDataKey(sealed, Bytes(kSecret), std::nullopt), key);
  EXPECT_EQ(OpenDataKey(sealed, Bytes("another"), std::nullopt).error(),
            OpenError::kOtherSecret);
  EXPECT_FALSE(OpenDataKey(sealed, Bytes(kSecret), "1234").has_value());
}

// Sealed with a PIN, the key opens with the secret and that PIN only.
TEST(DataKeyTest, WithPin) {
  const Key key = MakeDataKey();
  const SealedKey sealed = SealDataKey(key, Bytes(kSecret), "908172");

  ASSERT_TRUE(sealed.pin);
  EXPECT_EQ(sealed.pin->rounds, kPinRounds);
  EXPECT_FALSE(sealed.box.empty());
  EXPECT_EQ(OpenDataKey(sealed, Bytes(kSecret), "908172"), key);
  EXPECT_FALSE(OpenDataKey(sealed, Bytes(kSecret), "908173").has_value());
  EXPECT_FALSE(OpenDataKey(sealed, Bytes(kSecret), std::nullopt).has_value());
  // Another secret is told apart from a wrong PIN, which a wipe counts.
  EXPECT_EQ(OpenDataKey(sealed, Bytes("another"), "908172").error(),
            OpenError::kOtherSecret);
  EXPECT_EQ(OpenDataKey(sealed, Bytes(kSecret), "908173").error(),
            OpenError::kRefused);
}

// A new PIN reseals the same key, which the old PIN no longer opens.
TEST(DataKeyTest, Reseal) {
  const Key key = MakeDataKey();
  const SealedKey first = SealDataKey(key, Bytes(kSecret), "1234");
  const SealedKey second =
      SealDataKey(OpenDataKey(first, Bytes(kSecret), "1234").value(),
                  Bytes(kSecret), "5678");

  EXPECT_NE(second.pin->salt, first.pin->salt);
  EXPECT_EQ(OpenDataKey(second, Bytes(kSecret), "5678"), key);
  EXPECT_FALSE(OpenDataKey(second, Bytes(kSecret), "1234").has_value());
}

// A box changed since it was sealed does not open.
TEST(DataKeyTest, Tampered) {
  SealedKey sealed = SealDataKey(MakeDataKey(), Bytes(kSecret), std::nullopt);
  std::vector<uint8_t> box = base::Base64Decode(sealed.box).value();
  box.back() ^= 0x01;
  sealed.box = base::Base64Encode(box);

  EXPECT_FALSE(OpenDataKey(sealed, Bytes(kSecret), std::nullopt).has_value());
}

// Each use has a key of its own, the same for the same data key.
TEST(DataKeyTest, Derived) {
  const Key key = MakeDataKey();
  const std::string passphrase = VolumePassphrase(key);

  EXPECT_EQ(FilesKey(key), FilesKey(key));
  EXPECT_NE(FilesKey(key), key);
  EXPECT_EQ(passphrase, VolumePassphrase(key));
  EXPECT_NE(passphrase, VolumePassphrase(MakeDataKey()));
  const Key files = FilesKey(key);
  EXPECT_NE(base::Base64Decode(passphrase).value(),
            std::vector<uint8_t>(files.begin(), files.end()));
}

// Ported from PaneKit's SealedFileTests.

base::Value Tabs() {
  return base::Value(
      base::DictValue().Set("inbox", "https://mail.example.com/inbox"));
}

// Writes nothing in the clear, and reads back with its key only.
TEST(SealedFileTest, RoundTrip) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const SealedFile file(directory.GetPath(), "profiles");
  const Key key = FilesKey(MakeDataKey());

  ASSERT_TRUE(file.Save(Tabs(), key));

  std::string written;
  ASSERT_TRUE(base::ReadFileToString(file.path(), &written));
  EXPECT_EQ(written.find("mail.example.com"), std::string::npos);
  EXPECT_EQ(file.Load(key).value(), Tabs());
  EXPECT_EQ(file.Load(FilesKey(MakeDataKey())).error(), FileError::kRefused);
}

// A box moved to another file does not open.
TEST(SealedFileTest, Moved) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const Key key = FilesKey(MakeDataKey());
  const SealedFile profiles(directory.GetPath(), "profiles");
  const SealedFile deletions(directory.GetPath(), "deletions");

  ASSERT_TRUE(profiles.Save(Tabs(), key));
  ASSERT_TRUE(base::CopyFile(profiles.path(), deletions.path()));

  EXPECT_FALSE(deletions.Load(key).has_value());
}

// A plain file, as PersistedFile writes it, does not read.
TEST(SealedFileTest, Plain) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());

  ASSERT_TRUE(PersistedFile(directory.GetPath(), "profiles").Save(Tabs()));

  EXPECT_FALSE(SealedFile(directory.GetPath(), "profiles")
                   .Load(FilesKey(MakeDataKey()))
                   .has_value());
}

// A file not written yet reads as nothing.
TEST(SealedFileTest, Missing) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());

  EXPECT_EQ(SealedFile(directory.GetPath(), "profiles")
                .Load(FilesKey(MakeDataKey()))
                .value(),
            std::nullopt);
}

// The profiles the Swift Pane wrote into test_data/swift/profiles.json
// (PaneKit's SwiftVectorsTests' `profiles`), every field spelled out, the
// Swift's defaults included: a field a reader or a writer dropped fails here.
std::vector<BrowserProfile> SwiftProfiles() {
  BrowserProfile personal;
  personal.id = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02";
  personal.name = "Personal";
  personal.tabs = {Tab{.id = "9b1f3c2e-1d4a-4f6b-8c7e-2a5d9e0f1b3c",
                       .url = "https://www.google.com/"}};
  personal.created_at = "2026-11-09T04:00:00.000Z";
  personal.updated_at = "2026-11-09T04:00:00.000Z";

  Fingerprint fingerprint;
  fingerprint.user_agent =
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
      "like Gecko) Chrome/158.0.0.0 Safari/537.36";
  fingerprint.platform = Platform::kWindows;
  fingerprint.screen = {.width = 1920, .height = 1080};
  fingerprint.language = "pt-BR";
  fingerprint.languages = {"pt-BR", "pt", "en"};
  fingerprint.timezone = "America/Sao_Paulo";
  fingerprint.webgl =
      Fingerprint::WebGL{.vendor = "Google Inc. (NVIDIA)",
                         .renderer = "ANGLE (NVIDIA GeForce RTX 3060)"};
  fingerprint.hardware_concurrency = 8;
  fingerprint.device_memory = 8;
  fingerprint.max_touch_points = 0;
  fingerprint.canvas_noise = false;

  ProxyConfig proxy;
  proxy.type = ProxyType::kSocks5;
  proxy.host = "proxy.example.com";
  proxy.port = 1080;
  proxy.username = "user";
  proxy.password = "p@ss/<word>";

  BrowserProfile work;
  work.id = "0c4e8a2b-7f1d-4e3a-b6c9-5d2f8a1e7b40";
  work.name = "Café <Work> / ✓";
  work.color = ProfileColor::kViolet;
  work.group = "Clients";
  work.fingerprint = fingerprint;
  work.proxy = proxy;
  work.keep_loaded = true;
  // Made with history off, it kept nothing, so owes no clear: Swift's didSet
  // doesn't run in an initializer.
  work.saves_history = false;
  work.clears_history = false;
  work.tabs = {
      Tab{.id = "3e7a1c9d-2b4f-4a6e-8d1c-7f3b5a9e2c10",
          .url = "https://example.com/a?b=c&d=</script>",
          .title = "Example — “quoted”",
          .favicon = "data:image/png;base64,iVBORw0KGgo="},
      Tab{.id = "5c2e9a7b-1f3d-4b8e-a6c2-9d4f1b7e3a58", .url = "about:blank"}};
  work.created_at = "2026-10-01T12:30:00.000Z";
  work.updated_at = "2026-11-08T23:59:59.999Z";

  return {personal, work};
}

// The files and keys the Swift Pane wrote (PaneKit's SwiftVectorsTests),
// read byte for byte: its boxes open, its derived keys are these, and its
// files decode to what it wrote (docs/plans/views-migration.md, C1).
TEST(SwiftVectorsTest, Vectors) {
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  const base::FilePath directory =
      root.AppendASCII("pane/model/test_data/swift");
  std::string text;
  ASSERT_TRUE(
      base::ReadFileToString(directory.AppendASCII("vectors.json"), &text));
  const base::DictValue vectors =
      std::move(base::JSONReader::Read(text, base::JSON_PARSE_RFC)->GetDict());

  Key key;
  const std::vector<uint8_t> data_key =
      base::Base64Decode(*vectors.FindString("dataKey")).value();
  ASSERT_EQ(data_key.size(), kKeySize);
  std::ranges::copy(data_key, key.begin());
  const std::string secret = *vectors.FindString("secret");
  const std::string pin = *vectors.FindString("pin");

  EXPECT_EQ(base::HexEncodeLower(FilesKey(key)),
            *vectors.FindString("filesKey"));
  EXPECT_EQ(
      base::HexEncodeLower(EngineFilesKey(key, *vectors.FindString("profile"))),
      *vectors.FindString("engineKey"));
  EXPECT_EQ(VolumePassphrase(key), *vectors.FindString("volumePassphrase"));
  EXPECT_EQ(FormerVolumePassphrase(key),
            *vectors.FindString("formerVolumePassphrase"));

  const std::optional<SealedKey> with_pin =
      SealedKey::FromValue(*vectors.FindDict("sealedWithPin"));
  const std::optional<SealedKey> without_pin =
      SealedKey::FromValue(*vectors.FindDict("sealedWithoutPin"));
  ASSERT_TRUE(with_pin && without_pin);
  EXPECT_EQ(OpenDataKey(*with_pin, Bytes(secret), pin), key);
  EXPECT_EQ(OpenDataKey(*with_pin, Bytes(secret), "908173").error(),
            OpenError::kRefused);
  EXPECT_EQ(OpenDataKey(*without_pin, Bytes(secret), std::nullopt), key);
  // What C++ writes back of it is what Swift wrote.
  EXPECT_EQ(with_pin->ToValue(), *vectors.FindDict("sealedWithPin"));

  // profiles.json, sealed under the files key.
  const std::optional<base::Value> profiles_state =
      SealedFile(directory, "profiles").Load(FilesKey(key)).value();
  ASSERT_TRUE(profiles_state);
  const std::optional<std::vector<BrowserProfile>> profiles =
      ProfilesFromState(*profiles_state);
  ASSERT_TRUE(profiles);
  ASSERT_EQ(profiles->size(), 2u);
  const BrowserProfile& personal = (*profiles)[0];
  EXPECT_EQ(personal.name, "Personal");
  EXPECT_EQ(personal.color, ProfileColor::kBlue);
  EXPECT_FALSE(personal.fingerprint);
  EXPECT_TRUE(personal.saves_history);
  ASSERT_EQ(personal.tabs.size(), 1u);
  EXPECT_EQ(personal.tabs[0].url, "https://www.google.com/");
  const BrowserProfile& work = (*profiles)[1];
  EXPECT_EQ(work.name, "Café <Work> / ✓");
  EXPECT_EQ(work.color, ProfileColor::kViolet);
  EXPECT_EQ(work.group, "Clients");
  EXPECT_TRUE(work.keep_loaded);
  EXPECT_FALSE(work.saves_history);
  // Made with history off, it kept nothing, so owes no clear: Swift's
  // didSet doesn't run in an initializer.
  EXPECT_FALSE(work.clears_history);
  ASSERT_TRUE(work.fingerprint);
  EXPECT_EQ(work.fingerprint->platform, Platform::kWindows);
  EXPECT_EQ(work.fingerprint->screen,
            (Fingerprint::Screen{.width = 1920, .height = 1080}));
  EXPECT_EQ(work.fingerprint->languages,
            (std::vector<std::string>{"pt-BR", "pt", "en"}));
  EXPECT_EQ(work.fingerprint->timezone, "America/Sao_Paulo");
  EXPECT_EQ(
      work.fingerprint->webgl,
      (Fingerprint::WebGL{.vendor = "Google Inc. (NVIDIA)",
                          .renderer = "ANGLE (NVIDIA GeForce RTX 3060)"}));
  EXPECT_EQ(work.fingerprint->device_memory, 8);
  EXPECT_FALSE(work.fingerprint->canvas_noise);
  EXPECT_TRUE(work.fingerprint->audio_noise);
  ASSERT_TRUE(work.proxy);
  EXPECT_EQ(work.proxy->type, ProxyType::kSocks5);
  EXPECT_EQ(work.proxy->password, "p@ss/<word>");
  ASSERT_EQ(work.tabs.size(), 2u);
  EXPECT_EQ(work.tabs[0].url, "https://example.com/a?b=c&d=</script>");
  EXPECT_EQ(work.tabs[0].title, "Example — “quoted”");
  EXPECT_EQ(work.tabs[1].title, "");
  EXPECT_EQ(work.updated_at, "2026-11-08T23:59:59.999Z");
  // Every field, as the Swift test compared the whole value.
  EXPECT_EQ(*profiles, SwiftProfiles());
  // Decoded and encoded again, it reads the same.
  EXPECT_EQ(ProfilesFromState(ProfilesState(*profiles)), profiles);

  // settings.json, plain.
  const std::optional<base::Value> settings_state =
      PersistedFile(directory, "settings").Load().value();
  ASSERT_TRUE(settings_state);
  const std::optional<AppSettings> settings =
      SettingsFromState(*settings_state);
  ASSERT_TRUE(settings);
  EXPECT_EQ(settings->theme, Theme::kDark);
  EXPECT_EQ(settings->app_icon, AppIcon::kDusk);
  EXPECT_EQ(settings->tab_sleep_after_minutes, std::nullopt);
  EXPECT_EQ(settings->profile_unload_after_minutes, 45);
  EXPECT_EQ(settings->cache_budget_mb, 500);
  EXPECT_EQ(settings->lock_after_minutes, 5);
  EXPECT_EQ(settings->sidebar_width, 312.5);

  // security.json, plain, its key the one sealed with the PIN.
  const std::optional<base::Value> security_state =
      PersistedFile(directory, "security").Load().value();
  ASSERT_TRUE(security_state);
  const std::optional<SecurityState> security =
      SecurityState::FromValue(*security_state);
  ASSERT_TRUE(security);
  EXPECT_EQ(security->pin, (Pin{.length = 6}));
  EXPECT_EQ(security->failed_attempts, 2);
  EXPECT_EQ(security->key, with_pin);
  EXPECT_EQ(SecurityState::FromValue(security->ToValue()), security);
}

// The profiles a Mac Pane saved go through the sheet untouched as they were,
// byte for byte (docs/plans/views-migration.md, N4): the platform defaults
// are the one Pane runs on, which changes nothing a Mac profile holds, and
// the sheet's missing Group keeps the saved one.
TEST(SwiftVectorsTest, SheetKeepsWhatAMacSaved) {
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  const base::FilePath directory =
      root.AppendASCII("pane/model/test_data/swift");
  std::string text;
  ASSERT_TRUE(
      base::ReadFileToString(directory.AppendASCII("vectors.json"), &text));
  const base::DictValue vectors =
      std::move(base::JSONReader::Read(text, base::JSON_PARSE_RFC)->GetDict());
  Key key;
  const std::vector<uint8_t> data_key =
      base::Base64Decode(*vectors.FindString("dataKey")).value();
  ASSERT_EQ(data_key.size(), kKeySize);
  std::ranges::copy(data_key, key.begin());
  const std::optional<std::vector<BrowserProfile>> profiles = ProfilesFromState(
      SealedFile(directory, "profiles").Load(FilesKey(key)).value().value());
  ASSERT_TRUE(profiles);

  std::vector<BrowserProfile> edited = *profiles;
  for (BrowserProfile& profile : edited) {
    ProfileDraft(profile).ApplyTo(profile, [] {
      ADD_FAILURE() << "regenerated a fingerprint";
      return FingerprintPreset(Platform::kLinux);
    });
  }

  EXPECT_EQ(edited, *profiles);
  EXPECT_EQ(PersistedFile::Encode(ProfilesState(edited)),
            PersistedFile::Encode(ProfilesState(*profiles)));
  EXPECT_EQ(ProfileDraft().platform, HostPlatform());
}

// The plain files the Swift Pane wrote, read and saved again here, are the
// bytes it wrote: an earlier Pane that saves again what it read (0.15.2 saves
// security.json and its copy at every launch) changes nothing of a file this
// Pane wrote, as files-migration checks.
TEST(SwiftVectorsTest, SavedAsSwiftSavesThem) {
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  const base::FilePath directory =
      root.AppendASCII("pane/model/test_data/swift");

  std::string security_text;
  ASSERT_TRUE(base::ReadFileToString(directory.AppendASCII("security.json"),
                                     &security_text));
  const std::optional<SecurityState> security = SecurityState::FromValue(
      PersistedFile(directory, "security").Load().value().value());
  ASSERT_TRUE(security);
  EXPECT_EQ(PersistedFile::Encode(security->ToValue()), security_text);

  std::string settings_text;
  ASSERT_TRUE(base::ReadFileToString(directory.AppendASCII("settings.json"),
                                     &settings_text));
  const std::optional<AppSettings> settings = SettingsFromState(
      PersistedFile(directory, "settings").Load().value().value());
  ASSERT_TRUE(settings);
  EXPECT_EQ(PersistedFile::Encode(SettingsState(*settings)), settings_text);

  // The default width, a whole number, which Swift writes as one.
  AppSettings whole = *settings;
  whole.sidebar_width = 260;
  std::string whole_text = settings_text;
  base::ReplaceFirstSubstringAfterOffset(&whole_text, 0, "312.5", "260");
  EXPECT_EQ(PersistedFile::Encode(SettingsState(whole)), whole_text);
}

}  // namespace
}  // namespace pane::model
