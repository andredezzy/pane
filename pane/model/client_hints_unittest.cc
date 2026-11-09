// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/client_hints.h"

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "pane/model/engine_fingerprint.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's ClientHintsTests (the Electron app's
// client-hints.test.ts).

constexpr std::string_view kRealUA =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/146.0.7680.65 Safari/537.36";

constexpr std::string_view kNvidiaRenderer =
    "ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)";

Fingerprint MakeFingerprint(Platform platform,
                            std::string_view user_agent,
                            std::string_view renderer = kNvidiaRenderer) {
  Fingerprint fingerprint;
  fingerprint.user_agent = std::string(user_agent);
  fingerprint.platform = platform;
  fingerprint.screen = {.width = 1920, .height = 1080};
  fingerprint.language = "en-US";
  fingerprint.languages = {"en-US", "en"};
  fingerprint.timezone = "America/New_York";
  fingerprint.webgl = Fingerprint::WebGL{.vendor = "Google Inc. (NVIDIA)",
                                         .renderer = std::string(renderer)};
  fingerprint.hardware_concurrency = 8;
  fingerprint.device_memory = 8;
  fingerprint.max_touch_points = 0;
  return fingerprint;
}

std::string Serialized(const std::vector<UABrand>& list) {
  std::vector<std::string> brands;
  for (const UABrand& brand : list) {
    brands.push_back(
        base::StrCat({"\"", brand.brand, "\";v=\"", brand.version, "\""}));
  }
  return base::JoinString(brands, ", ");
}

// The Sec-CH-UA-* header values (lowercase names) the hints serialize to, as
// Chrome writes them: the oracle for what the engine sends, which serializes
// the headers itself.
std::map<std::string, std::string> ClientHintHeaders(
    const UserAgentMetadata& hints) {
  return {
      {"sec-ch-ua", Serialized(hints.brands)},
      {"sec-ch-ua-full-version-list", Serialized(hints.full_version_list)},
      {"sec-ch-ua-full-version",
       base::StrCat({"\"", hints.full_version, "\""})},
      {"sec-ch-ua-platform", base::StrCat({"\"", hints.platform, "\""})},
      {"sec-ch-ua-platform-version",
       base::StrCat({"\"", hints.platform_version, "\""})},
      {"sec-ch-ua-arch", base::StrCat({"\"", hints.architecture, "\""})},
      {"sec-ch-ua-bitness", base::StrCat({"\"", hints.bitness, "\""})},
      {"sec-ch-ua-wow64", "?0"},
      {"sec-ch-ua-model", "\"\""},
      {"sec-ch-ua-form-factors", "\"Desktop\""},
      {"sec-ch-ua-mobile", hints.mobile ? "?1" : "?0"},
  };
}

bool HasBrand(const std::vector<UABrand>& brands,
              std::string_view brand,
              std::string_view version) {
  return std::ranges::find(brands, UABrand{.brand = std::string(brand),
                                           .version = std::string(version)}) !=
         brands.end();
}

// Ported from PaneKit's ReconcileChromeVersionTests.

// Rewrites a stale claimed Chrome version to the real engine's.
TEST(ReconcileChromeVersionTest, StaleVersion) {
  constexpr std::string_view kClaimed =
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
      "like Gecko) Chrome/136.0.0.0 Safari/537.36";

  EXPECT_EQ(ReconcileChromeVersion(kClaimed, kRealUA),
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/146.0.7680.65 Safari/537.36");
}

// Leaves the rest of the claimed UA (platform token, Safari suffix) intact.
TEST(ReconcileChromeVersionTest, RestIntact) {
  constexpr std::string_view kClaimed =
      "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
      "Chrome/136.0.0.0 Safari/537.36";

  EXPECT_EQ(ReconcileChromeVersion(kClaimed, kRealUA),
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like "
            "Gecko) Chrome/146.0.7680.65 Safari/537.36");
}

// Is a no-op when the claimed version already matches the engine.
TEST(ReconcileChromeVersionTest, MatchingVersion) {
  EXPECT_EQ(ReconcileChromeVersion(kRealUA, kRealUA), kRealUA);
}

// Returns the claimed UA unchanged when the real UA carries no Chrome token.
TEST(ReconcileChromeVersionTest, NoChromeToken) {
  constexpr std::string_view kClaimed =
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
      "like Gecko) Chrome/136.0.0.0 Safari/537.36";

  EXPECT_EQ(ReconcileChromeVersion(kClaimed, "Mozilla/5.0 (weird runtime)"),
            kClaimed);
}

// Replaces a bare major version too.
TEST(ReconcileChromeVersionTest, BareMajor) {
  EXPECT_EQ(
      ReconcileChromeVersion("Mozilla/5.0 Chrome/136 Safari/537.36", kRealUA),
      "Mozilla/5.0 Chrome/146.0.7680.65 Safari/537.36");
}

// Ported from PaneKit's ClientHintsTests.

// The brand list matches Chromium's for an unbranded build (Chromium's own
// expectations: user_agent_utils_unittest.cc,
// GenerateBrandVersionListUnbranded*).
TEST(ClientHintsTest, ChromiumBrandList) {
  EXPECT_EQ(Serialized(BrandList(84, std::nullopt, "84", false)),
            R"("Not;A=Brand";v="8", "Chromium";v="84")");
  EXPECT_EQ(Serialized(BrandList(84, std::nullopt, "84.0.0.0", true)),
            R"("Not;A=Brand";v="8.0.0.0", "Chromium";v="84.0.0.0")");
  EXPECT_EQ(Serialized(BrandList(85, std::nullopt, "85", false)),
            R"("Chromium";v="85", "Not=A?Brand";v="99")");
  EXPECT_EQ(Serialized(BrandList(85, std::nullopt, "85.0.0.0", true)),
            R"("Chromium";v="85.0.0.0", "Not=A?Brand";v="99.0.0.0")");
}

// Sec-CH-UA matches Google Chrome's: what real Google Chrome sent at each
// version.
TEST(ClientHintsTest, GoogleChromeBrands) {
  const std::vector<std::pair<int, std::string_view>> kSent = {
      {120,
       R"("Not_A Brand";v="8", "Chromium";v="120", "Google Chrome";v="120")"},
      {124,
       R"("Chromium";v="124", "Google Chrome";v="124", "Not-A.Brand";v="99")"},
      {131,
       R"("Google Chrome";v="131", "Chromium";v="131", "Not_A Brand";v="24")"},
      {136,
       R"("Chromium";v="136", "Google Chrome";v="136", "Not.A/Brand";v="99")"},
  };
  for (const auto& [major, header] : kSent) {
    const std::string number = base::NumberToString(major);
    const UserAgentMetadata hints =
        ClientHints(MakeFingerprint(Platform::kWindows,
                                    base::StrCat({"Mozilla/5.0 Chrome/", number,
                                                  ".0.0.0 Safari/537.36"})),
                    base::StrCat({number, ".0.6099.71"}));

    EXPECT_EQ(ClientHintHeaders(hints)["sec-ch-ua"], header) << major;
  }
}

// The full version list carries the engine's full version.
TEST(ClientHintsTest, FullVersion) {
  const UserAgentMetadata hints =
      ClientHints(MakeFingerprint(Platform::kWindows,
                                  "Mozilla/5.0 Chrome/156.0.0.0 Safari/537.36"),
                  "156.0.8078.48");

  EXPECT_EQ(hints.full_version, "156.0.8078.48");
  EXPECT_EQ(
      Serialized(hints.full_version_list),
      R"("Not:A-Brand";v="8.0.0.0", "Chromium";v="156.0.8078.48", "Google Chrome";v="156.0.8078.48")");
  EXPECT_EQ(
      Serialized(hints.brands),
      R"("Not:A-Brand";v="8", "Chromium";v="156", "Google Chrome";v="156")");
}

// A claim for another major keeps its own full version.
TEST(ClientHintsTest, OtherMajor) {
  const UserAgentMetadata hints = ClientHints(
      MakeFingerprint(Platform::kWindows,
                      "Mozilla/5.0 Chrome/136.0.7103.92 Safari/537.36"),
      "156.0.8078.48");

  EXPECT_EQ(hints.full_version, "136.0.7103.92");
  EXPECT_TRUE(HasBrand(hints.brands, "Google Chrome", "136"));
}

// A UA without a Chrome token falls back to Chrome 136.
TEST(ClientHintsTest, NoChromeToken) {
  const UserAgentMetadata hints = ClientHints(
      MakeFingerprint(Platform::kWindows, "Mozilla/5.0 (weird runtime)"),
      "156.0.8078.48");

  EXPECT_EQ(hints.full_version, "136.0.0.0");
  EXPECT_TRUE(HasBrand(hints.brands, "Chromium", "136"));
}

// Platform and platform version follow the claimed OS; Linux reports none.
TEST(ClientHintsTest, Platforms) {
  const UserAgentMetadata on_windows = ClientHints(
      MakeFingerprint(Platform::kWindows, kRealUA), "146.0.7680.65");
  const UserAgentMetadata on_mac =
      ClientHints(MakeFingerprint(Platform::kMacOS, kRealUA), "146.0.7680.65");
  const UserAgentMetadata on_linux =
      ClientHints(MakeFingerprint(Platform::kLinux, kRealUA), "146.0.7680.65");

  EXPECT_EQ(on_windows.platform, "Windows");
  EXPECT_EQ(on_windows.platform_version, "15.0.0");
  EXPECT_EQ(on_mac.platform, "macOS");
  EXPECT_EQ(on_mac.platform_version, "14.6.1");
  EXPECT_EQ(on_linux.platform, "Linux");
  EXPECT_EQ(on_linux.platform_version, "");
}

// The architecture is arm for an Apple Silicon GPU, x86 otherwise.
TEST(ClientHintsTest, Architecture) {
  const Fingerprint apple_silicon = MakeFingerprint(
      Platform::kMacOS, kRealUA, "ANGLE (Apple, Apple M1, OpenGL 4.1)");
  const Fingerprint intel = MakeFingerprint(
      Platform::kMacOS, kRealUA,
      "ANGLE (Intel, Intel(R) Iris(TM) Plus Graphics, OpenGL 4.1)");

  EXPECT_EQ(ClientHints(apple_silicon, "146.0.7680.65").architecture, "arm");
  EXPECT_EQ(ClientHints(intel, "146.0.7680.65").architecture, "x86");
}

// The headers serialize as Chrome writes them.
TEST(ClientHintsTest, Headers) {
  std::map<std::string, std::string> headers = ClientHintHeaders(ClientHints(
      MakeFingerprint(Platform::kWindows, kRealUA), "146.0.7680.65"));

  EXPECT_EQ(headers["sec-ch-ua-platform"], R"("Windows")");
  EXPECT_EQ(headers["sec-ch-ua-platform-version"], R"("15.0.0")");
  EXPECT_EQ(headers["sec-ch-ua-full-version"], R"("146.0.7680.65")");
  EXPECT_EQ(headers["sec-ch-ua-arch"], R"("x86")");
  EXPECT_EQ(headers["sec-ch-ua-bitness"], R"("64")");
  EXPECT_EQ(headers["sec-ch-ua-mobile"], "?0");
  EXPECT_EQ(headers["sec-ch-ua-wow64"], "?0");
  EXPECT_EQ(headers["sec-ch-ua-model"], R"("")");
  EXPECT_EQ(headers["sec-ch-ua-form-factors"], R"("Desktop")");
}

// Ported from PaneKit's EngineFingerprintTests.

constexpr std::string_view kEngineUA =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36";

EngineFingerprint Engine(const Fingerprint& fingerprint,
                         std::string_view profile_id = "a") {
  return EngineFingerprint::For(fingerprint, profile_id, kEngineUA,
                                "156.0.8078.48");
}

// The User-Agent and its metadata follow the engine's version.
TEST(EngineFingerprintTest, UserAgent) {
  const EngineFingerprint engine =
      Engine(MakeFingerprint(Platform::kWindows,
                             "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                             "AppleWebKit/537.36 (KHTML, like Gecko) "
                             "Chrome/136.0.0.0 Safari/537.36"),
             "profile-a");

  EXPECT_EQ(engine.user_agent,
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36");
  EXPECT_EQ(engine.user_agent_metadata.full_version, "156.0.8078.48");
  EXPECT_EQ(engine.user_agent_metadata.platform, "Windows");
  EXPECT_EQ(engine.user_agent_metadata.form_factors,
            std::vector<std::string>{"Desktop"});
  EXPECT_EQ(engine.renderer.platform, "Win32");
}

// The available screen leaves room for each OS's bars.
TEST(EngineFingerprintTest, Screen) {
  Fingerprint windows = MakeFingerprint(Platform::kWindows, kRealUA);
  windows.screen = {.width = 1920, .height = 1080, .color_depth = 24};
  Fingerprint mac_os = MakeFingerprint(Platform::kMacOS, kRealUA);
  mac_os.screen = {.width = 1440, .height = 900, .color_depth = 30};

  EXPECT_EQ(EngineScreen(windows),
            (EngineFingerprint::Screen{.width = 1920,
                                       .height = 1080,
                                       .avail_width = 1920,
                                       .avail_height = 1032,
                                       .avail_left = 0,
                                       .avail_top = 0,
                                       .color_depth = 24}));
  EXPECT_EQ(EngineScreen(mac_os),
            (EngineFingerprint::Screen{.width = 1440,
                                       .height = 900,
                                       .avail_width = 1440,
                                       .avail_height = 875,
                                       .avail_left = 0,
                                       .avail_top = 25,
                                       .color_depth = 30}));
  EXPECT_EQ(
      EngineScreen(MakeFingerprint(Platform::kLinux, kRealUA)).avail_height,
      1053);
}

// Device memory is one Chrome reports: a power of two from 0.25 to 8.
TEST(EngineFingerprintTest, DeviceMemory) {
  EXPECT_EQ(ApproximatedDeviceMemory(8), 8);
  EXPECT_EQ(ApproximatedDeviceMemory(16), 8);
  EXPECT_EQ(ApproximatedDeviceMemory(6), 4);
  EXPECT_EQ(ApproximatedDeviceMemory(1), 1);
  EXPECT_EQ(ApproximatedDeviceMemory(0.1), 0.25);
}

// The noise seed is the Electron app's hash of the profile ID.
TEST(EngineFingerprintTest, NoiseSeed) {
  EXPECT_EQ(NoiseSeed("profile-a"), 1'005'401'795u);
  EXPECT_EQ(NoiseSeed("profile-b"), 1'005'401'794u);
  EXPECT_EQ(NoiseSeed("c4e1a2b3-0000-4000-8000-000000000001"), 1'229'998'108u);
  EXPECT_EQ(NoiseSeed("0b8f3c2e-5d1a-4e7b-9c6f-2a4d8e1f3b5c"), 485'437'020u);
  EXPECT_EQ(NoiseSeed(""), 0u);
}

// Pixel ratio and voices follow the claimed platform, on a Mac host.
TEST(EngineFingerprintTest, PlatformDerived) {
  const EngineFingerprint windows =
      Engine(MakeFingerprint(Platform::kWindows, kRealUA));
  const EngineFingerprint mac_os =
      Engine(MakeFingerprint(Platform::kMacOS, kRealUA));

  EXPECT_EQ(windows.renderer.device_pixel_ratio, 1);
  EXPECT_TRUE(windows.renderer.hide_speech_voices);
  EXPECT_EQ(mac_os.renderer.device_pixel_ratio, 2);
  EXPECT_FALSE(mac_os.renderer.hide_speech_voices);
  EXPECT_EQ(mac_os.renderer.platform, "MacIntel");
}

// Languages fall back to the language, and an empty timezone to none.
TEST(EngineFingerprintTest, LanguagesAndTimezone) {
  Fingerprint claimed = MakeFingerprint(Platform::kLinux, kRealUA);
  claimed.languages = {};
  claimed.language = "pt-BR";
  claimed.timezone = "";

  const EngineFingerprint engine = Engine(claimed);

  EXPECT_EQ(engine.languages, std::vector<std::string>{"pt-BR"});
  EXPECT_EQ(engine.renderer.locale, "pt-BR");
  EXPECT_EQ(engine.renderer.timezone, std::nullopt);
  EXPECT_EQ(engine.renderer.platform, "Linux x86_64");
}

// The JSON is stable and decodes back.
TEST(EngineFingerprintTest, Json) {
  const EngineFingerprint engine =
      Engine(MakeFingerprint(Platform::kWindows, kRealUA));
  const std::string text = engine.ToJson();

  EXPECT_EQ(engine.ToJson(), text);
  EXPECT_TRUE(text.starts_with(
      R"({"languages":["en-US","en"],"renderer":{"audioNoise":true)"));
  EXPECT_EQ(EngineFingerprint::FromJson(text), engine);
}

}  // namespace
}  // namespace pane::model
