// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_UPDATES_H_
#define PANE_MODEL_UPDATES_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "base/time/time.h"
#include "base/types/expected.h"
#include "url/gurl.h"

namespace pane::model {

// The update check (ADR 0002, and the Electron app's updater.ts, whose
// contract every Electron Pane still reads): the repo's latest GitHub
// Release, newer than the running app and carrying the arm64 dmg, is the one
// available update. Installing stays the user's: Pane downloads the dmg and
// opens it.
struct AvailableUpdate {
  friend bool operator==(const AvailableUpdate&,
                         const AvailableUpdate&) = default;

  std::string version;
  GURL dmg_url;
  // The dmg's SHA-256 in lowercase hex, as the release gives it.
  std::optional<std::string> sha256;
};

inline constexpr char kUpdaterUserAgent[] = "Pane-Updater";
inline constexpr std::string_view kDmgSuffix = "-arm64.dmg";

// A completed check runs again six hours later, as Chrome's and Brave's check
// within hours, so a security release reaches Pane soon; a failed one sooner.
inline constexpr base::TimeDelta kUpdateCheckInterval = base::Hours(6);
inline constexpr std::array<base::TimeDelta, 3> kUpdateRetryDelays = {
    base::Minutes(5), base::Minutes(15), base::Hours(1)};

// The checking state holds at least this long, so a click visibly does
// something even when the answer comes at once.
inline constexpr base::TimeDelta kMinimumChecking = base::Milliseconds(800);

// The repo's own release downloads: what a link from GitHub's JSON must name,
// since Pane opens the file it fetches. TLS vouches for the JSON, not for
// where its links go.
inline constexpr std::string_view kDownloadsPrefix =
    "/andredezzy/pane/releases/download/";

// The repo's latest release, in GitHub's API.
const GURL& ReleasesURL();

// Why a check found no answer.
enum class UpdateCheckError {
  // JSON that is not a release.
  kNotARelease,
  // A version that does not parse.
  kInvalidVersion,
};

// The update in GitHub's latest-release JSON (fetched from `releases_url`),
// or nullopt when it is not newer than `current_version`, or has no dmg Pane
// may download. An error for JSON that is not a release, or a version that
// does not parse.
base::expected<std::optional<AvailableUpdate>, UpdateCheckError>
AvailableUpdateIn(std::string_view release_json,
                  std::string_view current_version,
                  const GURL& releases_url = ReleasesURL());

// The hex of an asset's digest, which GitHub gives as "sha256:<hex>", or
// nullopt for another algorithm, or for anything but 64 hex digits.
std::optional<std::string> Sha256FromDigest(std::string_view digest);

// Whether Pane may download and open the dmg at `url`: over https, from the
// repo's releases on github.com, under a plain file name ending in the dmg's
// suffix. A stand-in release server (PANE_UPDATE_URL, the scenarios') serves
// its downloads from its own host.
bool IsTrustedDownload(const GURL& url,
                       const GURL& releases_url = ReleasesURL());

// A few coarse buckets rather than a live countdown (format-eta.ts): "~3min"
// can only step to "~2min".
std::string FormatETA(int seconds);

// When a download leaves its connection (ADR 0002). On a path that throttles
// some connections and not others, one that starts slow stays slow while the
// next gets the full rate. Slower than 1 MB/s over its window, a connection is
// left. Each new one that is no faster doubles the next window, and after four
// the download stays put: the link itself is the slow one then.
class DownloadPace {
 public:
  explicit DownloadPace(base::Time now);

  // How long the connection gets before it is judged.
  base::TimeDelta window() const;

  // A connection's window starts: a new one, then again at its first bytes,
  // since its setup (DNS, TLS, a redirect) says nothing of its rate.
  void Begin(int64_t received, base::Time now);

  // What is left of the window.
  base::TimeDelta TimeLeft(base::Time now) const;

  // At the end of its window, whether to leave the connection, one that can
  // be left: one that cannot spends no switch. The next window starts here
  // either way.
  bool ShouldSwitch(int64_t received, base::Time now, bool can_leave = true);

 private:
  int switches_ = 0;
  base::Time window_start_;
  int64_t window_bytes_ = 0;
};

// What a download's connection answered, as far as going on from where it got
// to on another is concerned (ADR 0002).
struct DownloadAnswer {
  // The HTTP status, or 0 before the connection has answered.
  int status = 0;
  // It takes ranges: a 206, or Accept-Ranges: bytes.
  bool offers_ranges = false;
  // It names the file's version, an ETag or a Last-Modified, which a range
  // request sends back (If-Range), so a file that changed comes whole.
  bool validated = false;
};

// Whether the rest can come from where a connection got to: its server takes
// ranges and names the file's version, and has not refused to go on from
// there before.
bool CanResume(const DownloadAnswer& answer, bool refused);

// Whether a connection judged slow can be left. One that can resume loses
// nothing, and nor does a resumed one that has not answered yet, where the
// last one stopped still standing. One that cannot gives up what it brought,
// so it is left only while that is nothing, or under a twentieth of the
// file's `total` (below 0 while unknown).
bool CanLeaveConnection(bool resumed,
                        const DownloadAnswer& answer,
                        bool refused,
                        int64_t received,
                        int64_t total);

// How the answer to a request for the rest from `asked` on reads: the rest
// (206 from there), the whole file again (200), or a refusal (anything else,
// an expired signed link's 403 among them).
enum class RangeAnswer {
  kRest,
  kWhole,
  kRefused,
};
RangeAnswer ReadRangeAnswer(int status,
                            std::optional<int64_t> range_start,
                            int64_t asked);

// A connection that drops goes on from where it got to after 2, 4, then 8 s,
// `drops` being those in a row with nothing brought since: long enough for a
// Wi-Fi switch, which would fail each at once. Nullopt after the third.
std::optional<base::TimeDelta> DropRetryDelay(int drops);

// A download's time left (updater.ts): the average rate since the first
// bytes, which bursts and stalls cannot spike, with hysteresis on top. None in
// the first second: the connection's setup would weigh on the rate, and a 160
// MB dmg started at "~150min". Then a shorter estimate shows at once; a longer
// one only once it held for five seconds.
class DownloadETA {
 public:
  DownloadETA();

  // A new connection: its rate says nothing of the last one's, whose average
  // would hold the time left high long after a crawl. What shows stays until
  // the new rate settles.
  void RestartRate();

  // Seconds left after `received` of `total` bytes at `now`, or nullopt until
  // the rate is known.
  std::optional<int> Update(int64_t received, int64_t total, base::Time now);

 private:
  std::optional<base::Time> first_bytes_at_;
  int64_t first_bytes_ = 0;
  std::optional<int> published_;
  std::optional<base::Time> increase_since_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_UPDATES_H_
