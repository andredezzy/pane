// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/time_zones.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "third_party/icu/source/common/unicode/strenum.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

namespace pane::model {

namespace {

constexpr std::array<std::string_view, 8> kRegionNames = {
    "Africa",    "America", "Asia",   "Atlantic",
    "Australia", "Europe",  "Indian", "Pacific"};

std::string Spaced(std::string_view text) {
  std::string spaced(text);
  std::ranges::replace(spaced, '_', ' ');
  return spaced;
}

// "America/Argentina/Buenos_Aires": Argentina, Buenos Aires. Within its
// region, what tells it from another of the same name.
std::string Place(std::string_view identifier) {
  std::vector<std::string_view> parts = base::SplitStringPiece(
      identifier, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (!parts.empty()) {
    parts.erase(parts.begin());
  }
  return Spaced(base::JoinString(parts, ", "));
}

// GMT−5, GMT+5:30, GMT+0. A zone ICU does not know is at GMT.
std::string Offset(std::string_view identifier, base::Time now) {
  const std::unique_ptr<icu::TimeZone> zone(
      icu::TimeZone::createTimeZone(icu::UnicodeString::fromUTF8(identifier)));
  int32_t raw = 0;
  int32_t daylight = 0;
  UErrorCode status = U_ZERO_ERROR;
  zone->getOffset(now.InMillisecondsFSinceUnixEpoch(), /*local=*/false, raw,
                  daylight, status);
  const int seconds = U_SUCCESS(status) ? (raw + daylight) / 1000 : 0;

  const int minutes = std::abs(seconds) / 60;
  const std::string hours = base::StrCat(
      {"GMT", seconds < 0 ? "−" : "+", base::NumberToString(minutes / 60)});
  return minutes % 60 == 0
             ? hours
             : base::StrCat({hours, base::StringPrintf(":%02d", minutes % 60)});
}

}  // namespace

std::vector<std::string> KnownTimeZones() {
  std::vector<std::string> identifiers;
  UErrorCode status = U_ZERO_ERROR;
  // ICU's canonical names are CLDR's, which keep a place's older name
  // (Asia/Calcutta): IANA's is the one people know, and the one a profile
  // stores.
  const std::unique_ptr<icu::StringEnumeration> zones(
      icu::TimeZone::createTimeZoneIDEnumeration(
          UCAL_ZONE_TYPE_CANONICAL_LOCATION, nullptr, nullptr, status));
  if (U_FAILURE(status)) {
    return identifiers;
  }
  while (const icu::UnicodeString* zone = zones->snext(status)) {
    icu::UnicodeString iana;
    UErrorCode iana_status = U_ZERO_ERROR;
    icu::TimeZone::getIanaID(*zone, iana, iana_status);
    std::string identifier;
    (U_SUCCESS(iana_status) ? iana : *zone).toUTF8String(identifier);
    identifiers.push_back(std::move(identifier));
  }
  return identifiers;
}

std::vector<TimeZoneRegion> TimeZoneRegions(
    const std::vector<std::string>& identifiers,
    base::Time now) {
  std::vector<TimeZoneRegion> regions;
  for (std::string_view name : kRegionNames) {
    const std::string prefix = base::StrCat({name, "/"});
    TimeZoneRegion region{.name = std::string(name)};
    for (const std::string& identifier : identifiers) {
      if (identifier.starts_with(prefix)) {
        region.choices.push_back(
            {.identifier = identifier,
             .title = base::StrCat(
                 {Place(identifier), " (", Offset(identifier, now), ")"})});
      }
    }
    if (region.choices.empty()) {
      continue;
    }
    std::ranges::sort(region.choices, {}, &TimeZoneChoice::title);
    regions.push_back(std::move(region));
  }
  return regions;
}

std::vector<TimeZoneRegion> TimeZoneRegions() {
  return TimeZoneRegions(KnownTimeZones(), base::Time::Now());
}

std::string CurrentTimeZone() {
  const std::unique_ptr<icu::TimeZone> zone(icu::TimeZone::createDefault());
  icu::UnicodeString id;
  zone->getID(id);
  icu::UnicodeString iana;
  UErrorCode status = U_ZERO_ERROR;
  icu::TimeZone::getIanaID(id, iana, status);
  std::string identifier;
  (U_SUCCESS(status) ? iana : id).toUTF8String(identifier);
  return identifier;
}

std::string TimeZoneCity(std::string_view identifier) {
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      identifier, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  return Spaced(parts.empty() ? identifier : parts.back());
}

}  // namespace pane::model
