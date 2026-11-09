// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/app_settings.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <string_view>
#include <utility>

namespace pane::model {

namespace {

constexpr std::array<std::string_view, 3> kThemes = {"system", "light", "dark"};
constexpr std::array<std::string_view, 11> kAppIcons = {
    "automatic", "day",      "clouds",    "morning", "dusk",    "fuchsia",
    "sunset",    "twilight", "afterglow", "night",   "moonrise"};

template <typename Enum, size_t N>
std::optional<Enum> FromName(const std::array<std::string_view, N>& names,
                             std::string_view name) {
  const auto it = std::ranges::find(names, name);
  return it == names.end() ? std::nullopt
                           : std::make_optional(static_cast<Enum>(
                                 std::distance(names.begin(), it)));
}

template <typename Enum, size_t N>
std::string_view NameOf(const std::array<std::string_view, N>& names,
                        Enum value) {
  return names[static_cast<size_t>(value)];
}

// An enum field: absent or null keeps the default, another name fails.
template <typename Enum, size_t N>
bool EnumField(const base::DictValue& dict,
               std::string_view key,
               const std::array<std::string_view, N>& names,
               Enum& out) {
  const base::Value* value = dict.Find(key);
  if (!value || value->is_none()) {
    return true;
  }
  const std::string* name = value->GetIfString();
  const std::optional<Enum> parsed =
      name ? FromName<Enum>(names, *name) : std::nullopt;
  if (!parsed) {
    return false;
  }
  out = *parsed;
  return true;
}

// Absent: the default. An explicit null: off.
bool OptionalIntField(const base::DictValue& dict,
                      std::string_view key,
                      std::optional<int>& out) {
  const base::Value* value = dict.Find(key);
  if (!value) {
    return true;
  }
  if (value->is_none()) {
    out = std::nullopt;
    return true;
  }
  if (!value->is_int()) {
    return false;
  }
  out = value->GetInt();
  return true;
}

base::Value OrNull(const std::optional<int>& value) {
  return value ? base::Value(*value) : base::Value();
}

}  // namespace

AppIcon Resolved(AppIcon icon, bool dark) {
  if (icon != AppIcon::kAutomatic) {
    return icon;
  }
  return dark ? AppIcon::kNight : AppIcon::kDay;
}

double ClampedSidebarWidth(double width) {
  return std::clamp(width, kSidebarMinWidth, kSidebarMaxWidth);
}

AppSettings::AppSettings() = default;
AppSettings::AppSettings(const AppSettings&) = default;
AppSettings& AppSettings::operator=(const AppSettings&) = default;
AppSettings::~AppSettings() = default;

// static
std::optional<AppSettings> AppSettings::FromValue(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  AppSettings settings;
  if (const base::Value* path = dict->Find("chromiumPath");
      path && !path->is_none()) {
    if (!path->is_string()) {
      return std::nullopt;
    }
    settings.chromium_path = path->GetString();
  }
  if (!EnumField(*dict, "theme", kThemes, settings.theme) ||
      !EnumField(*dict, "appIcon", kAppIcons, settings.app_icon) ||
      !OptionalIntField(*dict, "tabSleepAfterMinutes",
                        settings.tab_sleep_after_minutes) ||
      !OptionalIntField(*dict, "profileUnloadAfterMinutes",
                        settings.profile_unload_after_minutes) ||
      !OptionalIntField(*dict, "cacheBudgetMB", settings.cache_budget_mb) ||
      !OptionalIntField(*dict, "lockAfterMinutes",
                        settings.lock_after_minutes)) {
    return std::nullopt;
  }
  if (const base::Value* width = dict->Find("sidebarWidth");
      width && !width->is_none()) {
    const std::optional<double> points = width->GetIfDouble();
    if (!points) {
      return std::nullopt;
    }
    settings.sidebar_width = *points;
  }
  settings.sidebar_width = ClampedSidebarWidth(settings.sidebar_width);
  return settings;
}

base::Value AppSettings::ToValue() const {
  return base::Value(
      base::DictValue()
          .Set("chromiumPath", chromium_path)
          .Set("theme", NameOf(kThemes, theme))
          .Set("appIcon", NameOf(kAppIcons, app_icon))
          .Set("tabSleepAfterMinutes", OrNull(tab_sleep_after_minutes))
          .Set("profileUnloadAfterMinutes",
               OrNull(profile_unload_after_minutes))
          .Set("cacheBudgetMB", OrNull(cache_budget_mb))
          .Set("lockAfterMinutes", OrNull(lock_after_minutes))
          .Set("sidebarWidth", sidebar_width));
}

std::optional<AppSettings> SettingsFromState(const base::Value& state) {
  const base::DictValue* dict = state.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const base::Value* settings = dict->Find("settings");
  if (!settings || settings->is_none()) {
    return AppSettings();
  }
  return AppSettings::FromValue(*settings);
}

base::Value SettingsState(const AppSettings& settings) {
  return base::Value(base::DictValue().Set("settings", settings.ToValue()));
}

}  // namespace pane::model
