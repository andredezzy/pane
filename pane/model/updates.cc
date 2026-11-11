// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/updates.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "pane/model/value_reader.h"
#include "pane/model/versions.h"
#include "url/url_constants.h"

namespace pane::model {

namespace {

constexpr double kSlowBytesPerSecond = 1'000'000;
constexpr base::TimeDelta kFirstWindow = base::Seconds(5);
constexpr int kSwitchesInARow = 4;
constexpr int kDropsInARow = 3;

constexpr base::TimeDelta kSettle = base::Seconds(1);
constexpr base::TimeDelta kIncreaseHold = base::Seconds(5);

struct Asset {
  std::string name;
  std::string download_url;
  std::optional<std::string> digest;
};

}  // namespace

const GURL& ReleasesURL() {
  static const base::NoDestructor<GURL> url(
      "https://api.github.com/repos/andredezzy/pane/releases/latest");
  return *url;
}

base::expected<std::optional<AvailableUpdate>, UpdateCheckError>
AvailableUpdateIn(std::string_view release_json,
                  std::string_view current_version,
                  const GURL& releases_url) {
  const std::optional<base::Value> release =
      base::JSONReader::Read(release_json, base::JSON_PARSE_RFC);
  ValueReader reader(release ? &*release : nullptr);
  const std::string tag = reader.String("tag_name");
  const base::Value* assets = reader.Required("assets");
  const base::ListValue* list = assets ? assets->GetIfList() : nullptr;
  if (!reader.ok() || !list) {
    return base::unexpected(UpdateCheckError::kNotARelease);
  }

  std::optional<Asset> dmg;
  for (const base::Value& value : *list) {
    ValueReader asset(&value);
    Asset read{.name = asset.String("name"),
               .download_url = asset.String("browser_download_url"),
               .digest = asset.OptionalString("digest")};
    if (!asset.ok()) {
      return base::unexpected(UpdateCheckError::kNotARelease);
    }
    if (!dmg && read.name.ends_with(kDmgSuffix)) {
      dmg = std::move(read);
    }
  }

  std::string_view version = tag;
  if (version.starts_with('v') || version.starts_with('V')) {
    version.remove_prefix(1);
  }
  if (!dmg) {
    return std::nullopt;
  }
  const GURL dmg_url(dmg->download_url);
  if (!IsTrustedDownload(dmg_url, releases_url)) {
    return std::nullopt;
  }
  const base::expected<bool, InvalidVersion> newer =
      IsNewerVersion(version, current_version);
  if (!newer.has_value()) {
    return base::unexpected(UpdateCheckError::kInvalidVersion);
  }
  if (!*newer) {
    return std::nullopt;
  }
  return AvailableUpdate{
      .version = std::string(version),
      .dmg_url = dmg_url,
      .sha256 = dmg->digest ? Sha256FromDigest(*dmg->digest) : std::nullopt};
}

std::optional<std::string> Sha256FromDigest(std::string_view digest) {
  static constexpr std::string_view kPrefix = "sha256:";
  if (!digest.starts_with(kPrefix)) {
    return std::nullopt;
  }
  const std::string hex = base::ToLowerASCII(digest.substr(kPrefix.size()));
  const bool is_hex = std::ranges::all_of(
      hex, [](char digit) { return base::IsHexDigit(digit); });
  return hex.size() == 64 && is_hex ? std::make_optional(hex) : std::nullopt;
}

bool IsTrustedDownload(const GURL& url, const GURL& releases_url) {
  if (!url.is_valid()) {
    return false;
  }
  const std::string_view path = url.path();
  const std::string name =
      base::UnescapeBinaryURLComponent(path.substr(path.rfind('/') + 1));
  if (!name.ends_with(kDmgSuffix) || name.starts_with('.') ||
      name.find('/') != std::string::npos) {
    return false;
  }

  if (releases_url != ReleasesURL()) {
    return url.scheme() == releases_url.scheme() &&
           url.host() == releases_url.host();
  }

  // GURL has already resolved the path's dot segments: one still there was
  // escaped, which a server may read as a way up.
  const std::string unescaped = base::UnescapeBinaryURLComponent(path);
  return url.SchemeIs(url::kHttpsScheme) && url.host() == "github.com" &&
         !url.has_port() && unescaped.starts_with(kDownloadsPrefix) &&
         unescaped.find("/../") == std::string::npos;
}

std::string FormatETA(int seconds) {
  if (seconds < 45) {
    return "<1min";
  }
  const int minutes = std::max(1, static_cast<int>(std::round(seconds / 60.0)));
  return base::StrCat({"~", base::NumberToString(minutes), "min"});
}

DownloadPace::DownloadPace(base::Time now) : window_start_(now) {}

base::TimeDelta DownloadPace::window() const {
  return kFirstWindow * (1 << switches_);
}

void DownloadPace::Begin(int64_t received, base::Time now) {
  window_start_ = now;
  window_bytes_ = received;
}

base::TimeDelta DownloadPace::TimeLeft(base::Time now) const {
  return window() - (now - window_start_);
}

bool DownloadPace::ShouldSwitch(int64_t received,
                                base::Time now,
                                bool can_leave) {
  const double elapsed = (now - window_start_).InSecondsF();
  const double rate =
      elapsed > 0 ? static_cast<double>(received - window_bytes_) / elapsed : 0;
  Begin(received, now);

  if (rate >= kSlowBytesPerSecond) {
    switches_ = 0;
    return false;
  }
  if (!can_leave || switches_ >= kSwitchesInARow) {
    return false;
  }
  ++switches_;
  return true;
}

bool CanResume(const DownloadAnswer& answer, bool refused) {
  return !refused && answer.offers_ranges && answer.validated;
}

bool CanLeaveConnection(bool resumed,
                        const DownloadAnswer& answer,
                        bool refused,
                        int64_t received,
                        int64_t total) {
  return (resumed && answer.status == 0) || CanResume(answer, refused) ||
         received == 0 || (total > 0 && received * 20 < total);
}

RangeAnswer ReadRangeAnswer(int status,
                            std::optional<int64_t> range_start,
                            int64_t asked) {
  if (status == 206 && range_start == asked) {
    return RangeAnswer::kRest;
  }
  return status == 200 ? RangeAnswer::kWhole : RangeAnswer::kRefused;
}

std::optional<base::TimeDelta> DropRetryDelay(int drops) {
  if (drops < 0 || drops >= kDropsInARow) {
    return std::nullopt;
  }
  return base::Seconds(2 << drops);
}

DownloadETA::DownloadETA() = default;

void DownloadETA::RestartRate() {
  first_bytes_at_.reset();
  increase_since_.reset();
}

std::optional<int> DownloadETA::Update(int64_t received,
                                       int64_t total,
                                       base::Time now) {
  if (total <= 0 || received <= 0) {
    return published_;
  }
  if (!first_bytes_at_) {
    first_bytes_at_ = now;
    first_bytes_ = received;
    return published_;
  }

  const base::TimeDelta elapsed = now - *first_bytes_at_;
  if (elapsed < kSettle) {
    return published_;
  }
  const double rate =
      static_cast<double>(received - first_bytes_) / elapsed.InSecondsF();
  if (rate <= 0) {
    return published_;
  }

  const int raw = std::max(
      0, base::ClampRound<int>(static_cast<double>(total - received) / rate));
  if (!published_ || raw <= *published_) {
    published_ = raw;
    increase_since_.reset();
  } else if (increase_since_) {
    if (now - *increase_since_ > kIncreaseHold) {
      published_ = raw;
      increase_since_.reset();
    }
  } else {
    increase_since_ = now;
  }
  return published_;
}

}  // namespace pane::model
