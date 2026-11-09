// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/client_hints.h"

#include <array>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "third_party/re2/src/re2/re2.h"

namespace pane::model {

namespace {

// The major a User-Agent claims when it names no Chrome.
constexpr int64_t kFallbackMajor = 136;

// `seed`'s place among `count` choices.
size_t Pick(int64_t seed, size_t count) {
  return static_cast<size_t>(seed % static_cast<int64_t>(count));
}

std::optional<std::string> FirstGroup(std::string_view text,
                                      const re2::RE2& pattern) {
  std::string group;
  return re2::RE2::PartialMatch(text, pattern, &group)
             ? std::make_optional(group)
             : std::nullopt;
}

const re2::RE2& FullVersionPattern() {
  static const re2::LazyRE2 kPattern = {R"(Chrome/(\d+\.\d+\.\d+\.\d+))"};
  return *kPattern;
}

// Chrome reports an empty platform version on Linux (user_agent_utils.cc
// returns an empty string there); any other value paired with "Linux" is a
// tell. Windows and macOS report their OS version.
std::string_view PlatformVersion(Platform platform) {
  switch (platform) {
    case Platform::kWindows:
      return "15.0.0";
    case Platform::kMacOS:
      return "14.6.1";
    case Platform::kLinux:
      return "";
  }
}

bool IsAppleSilicon(const Fingerprint& fingerprint) {
  static const re2::LazyRE2 kAppleSilicon = {R"((?i)apple|\bm[1-4]\b|arm)"};
  return fingerprint.webgl &&
         re2::RE2::PartialMatch(fingerprint.webgl->renderer, *kAppleSilicon);
}

// GetRandomOrder in user_agent_utils.cc, for two or three brands.
std::vector<size_t> BrandOrder(int64_t seed, size_t count) {
  if (count == 2) {
    return {Pick(seed, 2), Pick(seed + 1, 2)};
  }
  static constexpr std::array<std::array<size_t, 3>, 6> kOrders = {{
      {0, 1, 2},
      {0, 2, 1},
      {1, 0, 2},
      {1, 2, 0},
      {2, 0, 1},
      {2, 1, 0},
  }};
  const std::array<size_t, 3>& order = kOrders[Pick(seed, kOrders.size())];
  return {order.begin(), order.end()};
}

}  // namespace

std::string ReconcileChromeVersion(std::string_view claimed_ua,
                                   std::string_view real_ua) {
  const std::optional<std::string> real_version =
      FirstGroup(real_ua, FullVersionPattern());
  if (!real_version) {
    return std::string(claimed_ua);
  }
  static const re2::LazyRE2 kClaimedVersion = {R"(Chrome/[\d.]+)"};
  std::string reconciled(claimed_ua);
  // The version is digits and dots, which a rewrite reads as they are.
  re2::RE2::Replace(&reconciled, *kClaimedVersion,
                    base::StrCat({"Chrome/", *real_version}));
  return reconciled;
}

std::string_view NavigatorPlatform(Platform platform) {
  switch (platform) {
    case Platform::kWindows:
      return "Win32";
    case Platform::kMacOS:
      return "MacIntel";
    case Platform::kLinux:
      return "Linux x86_64";
  }
}

UserAgentMetadata ClientHints(const Fingerprint& fingerprint,
                              std::string_view engine_version) {
  static const re2::LazyRE2 kMajor = {R"(Chrome/(\d+))"};
  const std::string major_text =
      FirstGroup(fingerprint.user_agent, *kMajor)
          .value_or(base::NumberToString(kFallbackMajor));
  int64_t major = 0;
  if (!base::StringToInt64(major_text, &major)) {
    major = kFallbackMajor;
  }

  const std::vector<std::string_view> engine_parts = base::SplitStringPiece(
      engine_version, ".", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  const std::string full_version =
      !engine_parts.empty() && engine_parts.front() == major_text
          ? std::string(engine_version)
          : FirstGroup(fingerprint.user_agent, FullVersionPattern())
                .value_or(base::StrCat({major_text, ".0.0.0"}));

  return UserAgentMetadata{
      .brands = BrandList(major, "Google Chrome", major_text,
                          /*full_versions=*/false),
      .full_version_list = BrandList(major, "Google Chrome", full_version,
                                     /*full_versions=*/true),
      .full_version = full_version,
      .platform = std::string(TitleOf(fingerprint.platform)),
      .platform_version = std::string(PlatformVersion(fingerprint.platform)),
      .architecture = IsAppleSilicon(fingerprint) ? "arm" : "x86",
      .bitness = "64",
      .model = "",
      .mobile = false,
      .wow64 = false,
      .form_factors = {"Desktop"}};
}

std::vector<UABrand> BrandList(int64_t seed,
                               std::optional<std::string_view> brand,
                               std::string_view version,
                               bool full_versions) {
  static constexpr std::array<std::string_view, 11> kGreaseChars = {
      " ", "(", ":", "-", ".", "/", ")", ";", "=", "?", "_"};
  static constexpr std::array<std::string_view, 3> kGreaseVersions = {"8", "99",
                                                                      "24"};
  const std::string_view grease_version =
      kGreaseVersions[Pick(seed, kGreaseVersions.size())];

  std::vector<UABrand> list = {
      {.brand = base::StrCat(
           {"Not", kGreaseChars[Pick(seed, kGreaseChars.size())], "A",
            kGreaseChars[Pick(seed + 1, kGreaseChars.size())], "Brand"}),
       .version = full_versions ? base::StrCat({grease_version, ".0.0.0"})
                                : std::string(grease_version)},
      {.brand = "Chromium", .version = std::string(version)},
  };
  if (brand) {
    list.push_back(
        {.brand = std::string(*brand), .version = std::string(version)});
  }

  const std::vector<size_t> order = BrandOrder(seed, list.size());
  std::vector<UABrand> shuffled = list;
  for (size_t index = 0; index < order.size(); ++index) {
    shuffled[order[index]] = list[index];
  }
  return shuffled;
}

}  // namespace pane::model
