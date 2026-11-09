// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_CLIENT_HINTS_H_
#define PANE_MODEL_CLIENT_HINTS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pane/model/profile.h"

namespace pane::model {

// A fingerprint's UA Client Hints (the Electron app's client-hints.ts): one
// derivation feeds both navigator.userAgentData and the Sec-CH-UA-* headers,
// so the two surfaces never disagree.

struct UABrand {
  friend bool operator==(const UABrand&, const UABrand&) = default;

  std::string brand;
  std::string version;
};

// What blink's UserAgentMetadata holds, as the engine reads it.
struct UserAgentMetadata {
  friend bool operator==(const UserAgentMetadata&,
                         const UserAgentMetadata&) = default;

  std::vector<UABrand> brands;
  std::vector<UABrand> full_version_list;
  std::string full_version;
  std::string platform;
  std::string platform_version;
  std::string architecture;
  std::string bitness;
  std::string model;
  bool mobile = false;
  bool wow64 = false;
  std::vector<std::string> form_factors;
};

// Rewrites the Chrome version a stored User-Agent claims to the engine's, so a
// fingerprint never advertises a Chrome build the engine is not: a stale claim
// is a contradiction Cloudflare Turnstile detects by probing the runtime. Any
// version form is replaced ("136.0.0.0" or a bare "136").
std::string ReconcileChromeVersion(std::string_view claimed_ua,
                                   std::string_view real_ua);

// navigator.platform for the claimed OS.
std::string_view NavigatorPlatform(Platform platform);

// The fingerprint's Client Hints, as Chrome of the version its User-Agent
// claims would send them. `engine_version` is the engine's full version
// ("156.0.8078.48"): Chrome reports it in the full version list, while its
// User-Agent only carries the major ("Chrome/156.0.0.0").
UserAgentMetadata ClientHints(const Fingerprint& fingerprint,
                              std::string_view engine_version);

// Chromium's brand list (GenerateBrandVersionList in user_agent_utils.cc): a
// GREASE brand, "Chromium", and the product brand, in a permutation seeded by
// the major version. Reproducing it keeps the list plausible for any version.
std::vector<UABrand> BrandList(int64_t seed,
                               std::optional<std::string_view> brand,
                               std::string_view version,
                               bool full_versions);

}  // namespace pane::model

#endif  // PANE_MODEL_CLIENT_HINTS_H_
