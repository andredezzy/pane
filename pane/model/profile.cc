// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/profile.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

#include "base/memory/raw_ptr.h"

namespace pane::model {

namespace {

constexpr std::array<std::string_view, 9> kColors = {
    "BLUE",   "ROSE", "EMERALD", "AMBER", "VIOLET",
    "ORANGE", "TEAL", "FUCHSIA", "ZINC"};
constexpr std::array<std::string_view, 9> kColorHexes = {
    "#60a5fa", "#fb7185", "#34d399", "#fbbf24", "#a78bfa",
    "#f97316", "#2dd4bf", "#e879f9", "#a1a1aa"};
constexpr std::array<std::string_view, 3> kPlatforms = {"WINDOWS", "MACOS",
                                                        "LINUX"};
constexpr std::array<std::string_view, 3> kPlatformTitles = {"Windows", "macOS",
                                                             "Linux"};
constexpr std::array<std::string_view, 4> kProxyTypes = {"HTTP", "HTTPS",
                                                         "SOCKS4", "SOCKS5"};

template <typename Enum, size_t N>
std::string_view NameOf(const std::array<std::string_view, N>& names,
                        Enum value) {
  return names[static_cast<size_t>(value)];
}

// Reads a JSON object as Swift's Decodable does: a required key absent or
// null fails, an optional one absent or null is nothing, and either of the
// wrong type fails. Any failure fails the whole value.
class Reader {
 public:
  explicit Reader(const base::Value* value)
      : dict_(value ? value->GetIfDict() : nullptr), ok_(dict_ != nullptr) {}

  bool ok() const { return ok_; }

  const base::Value* Present(std::string_view key) const {
    const base::Value* value = dict_ ? dict_->Find(key) : nullptr;
    return value && !value->is_none() ? value : nullptr;
  }

  const base::Value* Required(std::string_view key) {
    const base::Value* value = Present(key);
    ok_ = ok_ && value;
    return value;
  }

  std::string String(std::string_view key) {
    return Check(StringOf(Required(key))).value_or(std::string());
  }

  std::optional<std::string> OptionalString(std::string_view key) {
    const base::Value* value = Present(key);
    return value ? Check(StringOf(value)) : std::nullopt;
  }

  int Int(std::string_view key) {
    const base::Value* value = Required(key);
    return Check(value ? value->GetIfInt() : std::nullopt).value_or(0);
  }

  std::optional<int> OptionalInt(std::string_view key) {
    const base::Value* value = Present(key);
    return value ? Check(value->GetIfInt()) : std::nullopt;
  }

  double Double(std::string_view key) {
    const base::Value* value = Required(key);
    return Check(value ? value->GetIfDouble() : std::nullopt).value_or(0);
  }

  std::optional<bool> OptionalBool(std::string_view key) {
    const base::Value* value = Present(key);
    return value ? Check(value->GetIfBool()) : std::nullopt;
  }

  std::vector<std::string> Strings(std::string_view key) {
    std::vector<std::string> strings;
    const base::Value* value = Required(key);
    const base::ListValue* list = value ? value->GetIfList() : nullptr;
    ok_ = ok_ && list;
    if (list) {
      for (const base::Value& item : *list) {
        strings.push_back(Check(StringOf(&item)).value_or(std::string()));
      }
    }
    return strings;
  }

  template <typename Enum, size_t N>
  std::optional<Enum> OptionalEnum(
      std::string_view key,
      const std::array<std::string_view, N>& names) {
    const base::Value* value = Present(key);
    if (!value) {
      return std::nullopt;
    }
    const std::string* name = value->GetIfString();
    const auto it = name ? std::ranges::find(names, *name) : names.end();
    ok_ = ok_ && it != names.end();
    return it == names.end() ? std::nullopt
                             : std::make_optional(static_cast<Enum>(
                                   std::distance(names.begin(), it)));
  }

  template <typename Enum, size_t N>
  Enum RequiredEnum(std::string_view key,
                    const std::array<std::string_view, N>& names) {
    ok_ = ok_ && Present(key);
    return OptionalEnum<Enum>(key, names).value_or(Enum());
  }

  // A value another reader takes, and fails with.
  template <typename T>
  T Take(std::optional<T> value) {
    ok_ = ok_ && value.has_value();
    return value ? std::move(*value) : T();
  }

 private:
  static std::optional<std::string> StringOf(const base::Value* value) {
    const std::string* string = value ? value->GetIfString() : nullptr;
    return string ? std::make_optional(*string) : std::nullopt;
  }

  template <typename T>
  std::optional<T> Check(std::optional<T> value) {
    ok_ = ok_ && value.has_value();
    return value;
  }

  raw_ptr<const base::DictValue> dict_;
  bool ok_;
};

base::Value OrNull(const std::optional<std::string>& value) {
  return value ? base::Value(*value) : base::Value();
}

std::optional<Fingerprint::Screen> ScreenFromValue(const base::Value* value) {
  Reader reader(value);
  Fingerprint::Screen screen{
      .width = reader.Int("width"),
      .height = reader.Int("height"),
      .color_depth = reader.OptionalInt("colorDepth").value_or(24)};
  return reader.ok() ? std::make_optional(screen) : std::nullopt;
}

std::optional<Fingerprint::WebGL> WebGLFromValue(const base::Value* value) {
  Reader reader(value);
  Fingerprint::WebGL webgl{.vendor = reader.String("vendor"),
                           .renderer = reader.String("renderer")};
  return reader.ok() ? std::make_optional(webgl) : std::nullopt;
}

// A noise switch, {"noise": true}.
std::optional<bool> NoiseFromValue(const base::Value* value) {
  Reader reader(value);
  const std::optional<bool> noise = reader.OptionalBool("noise");
  return reader.ok() && noise ? noise : std::nullopt;
}

std::optional<Fingerprint> FingerprintFromValue(const base::Value* value) {
  Reader reader(value);
  Fingerprint fingerprint;
  fingerprint.user_agent = reader.String("userAgent");
  fingerprint.platform = reader.RequiredEnum<Platform>("platform", kPlatforms);
  fingerprint.screen = reader.Take(ScreenFromValue(reader.Required("screen")));
  fingerprint.language = reader.String("language");
  fingerprint.languages = reader.Strings("languages");
  fingerprint.timezone = reader.OptionalString("timeZone").value_or("");
  if (const base::Value* webgl = reader.Present("webgl")) {
    fingerprint.webgl = reader.Take(WebGLFromValue(webgl));
  }
  fingerprint.hardware_concurrency = reader.Int("hardwareConcurrency");
  fingerprint.device_memory = reader.Double("deviceMemory");
  fingerprint.max_touch_points = reader.Int("maxTouchPoints");
  if (const base::Value* canvas = reader.Present("canvas")) {
    fingerprint.canvas_noise = reader.Take(NoiseFromValue(canvas));
  }
  if (const base::Value* audio = reader.Present("audio")) {
    fingerprint.audio_noise = reader.Take(NoiseFromValue(audio));
  }
  return reader.ok() ? std::make_optional(std::move(fingerprint))
                     : std::nullopt;
}

base::Value FingerprintToValue(const Fingerprint& fingerprint) {
  base::ListValue languages;
  for (const std::string& language : fingerprint.languages) {
    languages.Append(language);
  }
  return base::Value(
      base::DictValue()
          .Set("userAgent", fingerprint.user_agent)
          .Set("platform", NameOf(kPlatforms, fingerprint.platform))
          .Set("screen", base::DictValue()
                             .Set("width", fingerprint.screen.width)
                             .Set("height", fingerprint.screen.height)
                             .Set("colorDepth", fingerprint.screen.color_depth))
          .Set("language", fingerprint.language)
          .Set("languages", std::move(languages))
          .Set("timeZone", fingerprint.timezone)
          .Set("timezone", fingerprint.timezone)
          .Set("webgl",
               fingerprint.webgl
                   ? base::Value(
                         base::DictValue()
                             .Set("vendor", fingerprint.webgl->vendor)
                             .Set("renderer", fingerprint.webgl->renderer))
                   : base::Value())
          .Set("hardwareConcurrency", fingerprint.hardware_concurrency)
          .Set("deviceMemory", fingerprint.device_memory)
          .Set("maxTouchPoints", fingerprint.max_touch_points)
          .Set("canvas",
               base::DictValue().Set("noise", fingerprint.canvas_noise))
          .Set("audio",
               base::DictValue().Set("noise", fingerprint.audio_noise)));
}

std::optional<ProxyConfig> ProxyFromValue(const base::Value* value) {
  Reader reader(value);
  ProxyConfig proxy;
  proxy.type = reader.RequiredEnum<ProxyType>("proxyType", kProxyTypes);
  proxy.host = reader.String("host");
  proxy.port = reader.Int("port");
  proxy.username = reader.OptionalString("username");
  proxy.password = reader.OptionalString("password");
  return reader.ok() ? std::make_optional(std::move(proxy)) : std::nullopt;
}

base::Value ProxyToValue(const ProxyConfig& proxy) {
  return base::Value(base::DictValue()
                         .Set("proxyType", NameOf(kProxyTypes, proxy.type))
                         .Set("host", proxy.host)
                         .Set("port", proxy.port)
                         .Set("username", OrNull(proxy.username))
                         .Set("password", OrNull(proxy.password)));
}

std::optional<Tab> TabFromValue(const base::Value* value) {
  Reader reader(value);
  Tab tab{.id = reader.String("id"),
          .url = reader.String("url"),
          .title = reader.OptionalString("title").value_or(""),
          .favicon = reader.OptionalString("favicon").value_or("")};
  return reader.ok() ? std::make_optional(std::move(tab)) : std::nullopt;
}

base::Value TabToValue(const Tab& tab) {
  return base::Value(base::DictValue()
                         .Set("id", tab.id)
                         .Set("url", tab.url)
                         .Set("title", tab.title)
                         .Set("favicon", tab.favicon));
}

}  // namespace

std::string_view HexOf(ProfileColor color) {
  return NameOf(kColorHexes, color);
}

std::string_view TitleOf(Platform platform) {
  return NameOf(kPlatformTitles, platform);
}

Fingerprint::Fingerprint() = default;
Fingerprint::Fingerprint(const Fingerprint&) = default;
Fingerprint& Fingerprint::operator=(const Fingerprint&) = default;
Fingerprint::~Fingerprint() = default;

ProxyConfig::ProxyConfig() = default;
ProxyConfig::ProxyConfig(const ProxyConfig&) = default;
ProxyConfig& ProxyConfig::operator=(const ProxyConfig&) = default;
ProxyConfig::~ProxyConfig() = default;

BrowserProfile::BrowserProfile() = default;
BrowserProfile::BrowserProfile(const BrowserProfile&) = default;
BrowserProfile& BrowserProfile::operator=(const BrowserProfile&) = default;
BrowserProfile::BrowserProfile(BrowserProfile&&) = default;
BrowserProfile& BrowserProfile::operator=(BrowserProfile&&) = default;
BrowserProfile::~BrowserProfile() = default;

// static
std::optional<BrowserProfile> BrowserProfile::FromValue(
    const base::Value& value) {
  Reader reader(&value);
  BrowserProfile profile;
  profile.id = reader.String("id");
  profile.name = reader.String("name");
  profile.color = reader.OptionalEnum<ProfileColor>("color", kColors)
                      .value_or(ProfileColor::kBlue);
  profile.group = reader.OptionalString("group");
  if (const base::Value* fingerprint = reader.Present("fingerprint")) {
    profile.fingerprint = reader.Take(FingerprintFromValue(fingerprint));
  }
  if (const base::Value* proxy = reader.Present("proxy")) {
    profile.proxy = reader.Take(ProxyFromValue(proxy));
  }
  profile.keep_loaded = reader.OptionalBool("keepLoaded").value_or(false);
  profile.saves_history = reader.OptionalBool("savesHistory").value_or(true);
  profile.clears_history =
      reader.OptionalBool("historyClearOwed").value_or(!profile.saves_history);
  const base::Value* tabs = reader.Required("tabs");
  const base::ListValue* list = tabs ? tabs->GetIfList() : nullptr;
  if (!list) {
    return std::nullopt;
  }
  for (const base::Value& tab : *list) {
    profile.tabs.push_back(reader.Take(TabFromValue(&tab)));
  }
  profile.created_at = reader.String("createdAt");
  profile.updated_at = reader.String("updatedAt");
  return reader.ok() ? std::make_optional(std::move(profile)) : std::nullopt;
}

base::Value BrowserProfile::ToValue() const {
  base::ListValue tab_values;
  for (const Tab& tab : tabs) {
    tab_values.Append(TabToValue(tab));
  }
  return base::Value(
      base::DictValue()
          .Set("id", id)
          .Set("name", name)
          .Set("color", NameOf(kColors, color))
          .Set("group", OrNull(group))
          .Set("fingerprint",
               fingerprint ? FingerprintToValue(*fingerprint) : base::Value())
          .Set("proxy", proxy ? ProxyToValue(*proxy) : base::Value())
          .Set("keepLoaded", keep_loaded)
          .Set("savesHistory", saves_history)
          .Set("historyClearOwed", clears_history)
          .Set("tabs", std::move(tab_values))
          .Set("createdAt", created_at)
          .Set("updatedAt", updated_at));
}

void BrowserProfile::SetSavesHistory(bool saves) {
  if (saves != saves_history) {
    clears_history = !saves;
  }
  saves_history = saves;
}

std::optional<std::vector<BrowserProfile>> ProfilesFromState(
    const base::Value& state) {
  Reader reader(&state);
  const base::Value* profiles = reader.Required("profiles");
  const base::ListValue* list = profiles ? profiles->GetIfList() : nullptr;
  if (!list) {
    return std::nullopt;
  }
  std::vector<BrowserProfile> decoded;
  for (const base::Value& profile : *list) {
    std::optional<BrowserProfile> one = BrowserProfile::FromValue(profile);
    if (!one) {
      return std::nullopt;
    }
    decoded.push_back(std::move(*one));
  }
  return decoded;
}

base::Value ProfilesState(const std::vector<BrowserProfile>& profiles) {
  base::ListValue list;
  for (const BrowserProfile& profile : profiles) {
    list.Append(profile.ToValue());
  }
  return base::Value(base::DictValue().Set("profiles", std::move(list)));
}

}  // namespace pane::model
