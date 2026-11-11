// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_TIME_ZONES_H_
#define PANE_MODEL_TIME_ZONES_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"

namespace pane::model {

// The zones a profile's fingerprint can claim, as the sheet's menu lists them
// (ADR 0038): by region, each with its offset today. Antarctica's, the
// Arctic's, and the bare "GMT" nobody would claim are left out; UTC stands on
// its own.

struct TimeZoneChoice {
  friend bool operator==(const TimeZoneChoice&,
                         const TimeZoneChoice&) = default;

  std::string identifier;
  // "New York (GMT−5)"
  std::string title;
};

struct TimeZoneRegion {
  friend bool operator==(const TimeZoneRegion&,
                         const TimeZoneRegion&) = default;

  std::string name;
  std::vector<TimeZoneChoice> choices;
};

inline constexpr char kUTCTimeZone[] = "UTC";

// The zones ICU knows a place for, each once, by its name in IANA's database.
std::vector<std::string> KnownTimeZones();

// `identifiers` by region, each titled with its offset at `now`, in the
// regions' order and sorted by title within each.
std::vector<TimeZoneRegion> TimeZoneRegions(
    const std::vector<std::string>& identifiers,
    base::Time now);
// The zones ICU knows, now.
std::vector<TimeZoneRegion> TimeZoneRegions();

// "America/Sao_Paulo": Sao Paulo. What the trigger and the note show.
std::string TimeZoneCity(std::string_view identifier);

// The computer's zone now, by its name in IANA's database, as ICU has it:
// what a profile on Automatic follows, and pages without a fingerprint.
std::string CurrentTimeZone();

}  // namespace pane::model

#endif  // PANE_MODEL_TIME_ZONES_H_
