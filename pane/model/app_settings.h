// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_APP_SETTINGS_H_
#define PANE_MODEL_APP_SETTINGS_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/values.h"

namespace pane::model {

// The app-wide appearance preference (CONTEXT.md: Theme). One knob: the app,
// its menus, and every page's `prefers-color-scheme` follow it.
enum class Theme { kSystem, kLight, kDark };

// The icon the Dock shows for Pane (CONTEXT.md: App icon): the window with one
// of ten skies, or automatic (ADR 0089). In Settings' order, the sky through a
// day.
enum class AppIcon {
  kAutomatic,
  kDay,
  kClouds,
  kMorning,
  kDusk,
  kFuchsia,
  kSunset,
  kTwilight,
  kAfterglow,
  kNight,
  kMoonrise,
};

// As settings.json spells them: "system", "light", "dark"; "automatic",
// "day", and the rest. nullopt for a name no Pane writes.
std::string_view ThemeName(Theme theme);
std::optional<Theme> ThemeNamed(std::string_view name);
std::string_view AppIconName(AppIcon icon);

// The window shown: automatic takes Day under a light appearance and Night
// under a dark one, the app's own (Theme) or, for system, the Mac's.
AppIcon Resolved(AppIcon icon, bool dark);

// The sidebar's width, which its edge resizes: 260 points by default
// (Aside's), between 200 and 400.
inline constexpr double kSidebarWidth = 260;
inline constexpr double kSidebarMinWidth = 200;
inline constexpr double kSidebarMaxWidth = 400;
double ClampedSidebarWidth(double width);

// Settings persisted in settings.json. A file written before a field existed
// keeps that field's default, while an explicit null still means "off".
struct AppSettings {
  AppSettings();
  AppSettings(const AppSettings&);
  AppSettings& operator=(const AppSettings&);
  ~AppSettings();

  // nullopt for a field of the wrong type, or a value no Pane writes.
  static std::optional<AppSettings> FromValue(const base::Value& value);
  // Every field, null for off.
  base::Value ToValue() const;

  friend bool operator==(const AppSettings&, const AppSettings&) = default;

  // The Electron app's Chromium override, which nothing here reads: kept so
  // settings.json round-trips with the Electron app's fields.
  std::string chromium_path;
  Theme theme = Theme::kSystem;
  AppIcon app_icon = AppIcon::kAutomatic;
  // Minutes a background tab stays loaded before it sleeps; nullopt = off.
  std::optional<int> tab_sleep_after_minutes = 15;
  // Minutes a hidden profile stays loaded before it unloads; nullopt = off.
  std::optional<int> profile_unload_after_minutes = 30;
  // Megabytes of cache a profile may hold before an unload trims it; nullopt
  // = off.
  std::optional<int> cache_budget_mb = 300;
  // Minutes without use before Pane locks, with a PIN set; nullopt = never by
  // time (it still locks when the Mac sleeps or locks, and on Lock Pane).
  std::optional<int> lock_after_minutes = 15;
  // Points, within the sidebar's range.
  double sidebar_width = kSidebarWidth;
};

// settings.json's state: {"settings": …}, the defaults when it holds none.
std::optional<AppSettings> SettingsFromState(const base::Value& state);
base::Value SettingsState(const AppSettings& settings);

}  // namespace pane::model

#endif  // PANE_MODEL_APP_SETTINGS_H_
