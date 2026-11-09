// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PROFILE_H_
#define PANE_MODEL_PROFILE_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

namespace pane::model {

// The persisted profile model. Field names and raw values match the Electron
// app's profiles.json, so its file decodes as-is, and the defaults below are
// the ones its loader applies to older files (ADR 0122).

enum class ProfileColor {
  kBlue,
  kRose,
  kEmerald,
  kAmber,
  kViolet,
  kOrange,
  kTeal,
  kFuchsia,
  kZinc,
};

// Fixed-color swatches: they do not follow the theme (DESIGN.md).
std::string_view HexOf(ProfileColor color);

// As profiles.json spells it, "BLUE" to "ZINC"; nullopt for a name no Pane
// writes.
std::string_view ColorName(ProfileColor color);
std::optional<ProfileColor> ColorNamed(std::string_view name);

enum class Platform { kWindows, kMacOS, kLinux };

// As each spells its own name.
std::string_view TitleOf(Platform platform);

enum class ProxyType { kHttp, kHttps, kSocks4, kSocks5 };

struct Fingerprint {
  struct Screen {
    int width = 0;
    int height = 0;
    int color_depth = 24;

    friend bool operator==(const Screen&, const Screen&) = default;
  };

  struct WebGL {
    std::string vendor;
    std::string renderer;

    friend bool operator==(const WebGL&, const WebGL&) = default;
  };

  Fingerprint();
  Fingerprint(const Fingerprint&);
  Fingerprint& operator=(const Fingerprint&);
  ~Fingerprint();

  friend bool operator==(const Fingerprint&, const Fingerprint&) = default;

  std::string user_agent;
  Platform platform = Platform::kWindows;
  Screen screen;
  std::string language;
  std::vector<std::string> languages;
  // The zone it claims; empty, it claims none, and follows the computer.
  // Under its own key, "timeZone": under "timezone", every file holds a
  // preset's zone, which nobody chose, so a file without this one follows
  // the computer (ADR 0038). "timezone" is still written, as Pane before 0.17
  // requires it.
  std::string timezone;
  std::optional<WebGL> webgl;
  int hardware_concurrency = 0;
  double device_memory = 0;
  int max_touch_points = 0;
  bool canvas_noise = true;
  bool audio_noise = true;
};

struct ProxyConfig {
  ProxyConfig();
  ProxyConfig(const ProxyConfig&);
  ProxyConfig& operator=(const ProxyConfig&);
  ~ProxyConfig();

  friend bool operator==(const ProxyConfig&, const ProxyConfig&) = default;

  ProxyType type = ProxyType::kHttp;
  std::string host;
  int port = 0;
  std::optional<std::string> username;
  std::optional<std::string> password;
};

struct Tab {
  friend bool operator==(const Tab&, const Tab&) = default;

  std::string id;
  std::string url;
  std::string title;
  // A data: URL, or empty while the page has none.
  std::string favicon;
};

struct BrowserProfile {
  BrowserProfile();
  BrowserProfile(const BrowserProfile&);
  BrowserProfile& operator=(const BrowserProfile&);
  BrowserProfile(BrowserProfile&&);
  BrowserProfile& operator=(BrowserProfile&&);
  ~BrowserProfile();

  // nullopt for a profile missing a field it requires, or with one of the
  // wrong type: a profiles.json with one fails as a whole.
  static std::optional<BrowserProfile> FromValue(const base::Value& value);
  // Explicit nulls, as the Electron app writes them.
  base::Value ToValue() const;

  // Turning history off owes a clear of what was kept, until the engine says
  // it is gone; turning it on again owes none (ADR 0119).
  void SetSavesHistory(bool saves);

  friend bool operator==(const BrowserProfile&,
                         const BrowserProfile&) = default;

  std::string id;
  std::string name;
  ProfileColor color = ProfileColor::kBlue;
  std::optional<std::string> group;
  // nullopt = no spoofing: the profile presents the real Chrome surface.
  std::optional<Fingerprint> fingerprint;
  std::optional<ProxyConfig> proxy;
  // Exempts the profile from automatic sleep and unload (CONTEXT.md).
  bool keep_loaded = false;
  // Whether it keeps a record of where it went (ADR 0119). A file from
  // before the choice has none: it kept its history, and still does.
  bool saves_history = true;
  // Turned off, what it kept is still to clear, until the engine says it is
  // gone: a lock or a quit before then clears at the next open. A file
  // without the key ("historyClearOwed"), from before 0.17.1, owes one if the
  // profile is off, for what those versions neither kept off nor cleared.
  bool clears_history = false;
  std::vector<Tab> tabs;
  std::string created_at;
  std::string updated_at;
};

// Whether `id` can name a profile: it becomes a folder under the engine's
// user data dir, the volume, and the caches, so it names that one folder and
// nothing else, with either separator, and is no longer than a file name can
// be (255 bytes on APFS). Every operation that deletes or moves a profile's
// files checks it, whatever file the id came from.
bool IsProfileFolderName(std::string_view id);

// profiles.json's state: {"profiles": […]}. nullopt when one profile fails.
std::optional<std::vector<BrowserProfile>> ProfilesFromState(
    const base::Value& state);
base::Value ProfilesState(const std::vector<BrowserProfile>& profiles);

}  // namespace pane::model

#endif  // PANE_MODEL_PROFILE_H_
