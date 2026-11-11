// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/updates.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "pane/model/update_install.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's UpdatesTests.

constexpr std::string_view kGitHubDownloads =
    "https://github.com/andredezzy/pane/releases/download";

std::string Release(std::string_view tag,
                    const std::vector<std::string>& assets,
                    std::string_view downloads = kGitHubDownloads,
                    const std::optional<std::string>& digest = std::nullopt) {
  const std::string digest_field =
      digest ? base::StrCat({R"(,"digest":")", *digest, "\""}) : "";
  std::vector<std::string> list;
  for (const std::string& asset : assets) {
    list.push_back(base::StrCat(
        {R"({"name":")", asset, R"(","browser_download_url":")", downloads, "/",
         tag, "/", asset, "\"", digest_field, "}"}));
  }
  return base::StrCat({R"({"tag_name":")", tag,
                       R"(","published_at":"2026-10-20T12:00:00Z","assets":[)",
                       base::JoinString(list, ","), "]}"});
}

std::optional<AvailableUpdate> Available(
    const std::string& release,
    const GURL& releases_url = ReleasesURL()) {
  return AvailableUpdateIn(release, "0.2.4", releases_url).value();
}

// A newer release with the arm64 dmg is the available update.
TEST(UpdatesTest, Newer) {
  const std::optional<AvailableUpdate> update =
      Available(Release("v0.3.0", {"Pane-0.3.0-x64.dmg", "Pane-0.3.0-arm64.dmg",
                                   "Pane-0.3.0.exe"}));

  ASSERT_TRUE(update);
  EXPECT_EQ(update->version, "0.3.0");
  EXPECT_EQ(update->dmg_url.ExtractFileName(), "Pane-0.3.0-arm64.dmg");
}

// The same or an older release is none.
TEST(UpdatesTest, NotNewer) {
  EXPECT_EQ(Available(Release("v0.2.4", {"Pane-0.2.4-arm64.dmg"})),
            std::nullopt);
  EXPECT_EQ(Available(Release("v0.2.3", {"Pane-0.2.3-arm64.dmg"})),
            std::nullopt);
}

// A release without the arm64 dmg is none.
TEST(UpdatesTest, NoDmg) {
  EXPECT_EQ(Available(Release("v1.0.0", {"Pane-1.0.0-x64.dmg"})), std::nullopt);
}

// A dmg Pane may not download is no update.
TEST(UpdatesTest, UntrustedDownloads) {
  for (std::string_view downloads : {
           "http://github.com/andredezzy/pane/releases/download",
           "https://evil.example/andredezzy/pane/releases/download",
           "https://github.com/someone/else/releases/download",
           "https://github.com/andredezzy/pane/releases/download/../../../"
           "someone/else/releases/download",
           "https://github.com:8443/andredezzy/pane/releases/download",
       }) {
    EXPECT_EQ(Available(Release("v1.0.0", {"Pane-1.0.0-arm64.dmg"}, downloads)),
              std::nullopt)
        << downloads;
  }

  EXPECT_FALSE(IsTrustedDownload(
      GURL("https://github.com/andredezzy/pane/releases/download/v1/"
           "-arm64.dmg/..")));
  EXPECT_FALSE(IsTrustedDownload(
      GURL("https://github.com/andredezzy/pane/releases/download/v1/"
           ".hidden-arm64.dmg")));
}

// A stand-in release server serves its downloads from its own host.
TEST(UpdatesTest, StandIn) {
  const GURL server("http://127.0.0.1:4100/releases/latest");
  const std::string local = Release("v1.0.0", {"Pane-1.0.0-arm64.dmg"},
                                    "http://127.0.0.1:4200/releases/download");

  EXPECT_EQ(Available(local, server).value_or(AvailableUpdate()).version,
            "1.0.0");
  EXPECT_EQ(Available(local), std::nullopt);
}

// The dmg's SHA-256 comes from its digest, when GitHub gives one.
TEST(UpdatesTest, Digest) {
  std::string hex;
  for (int index = 0; index < 32; ++index) {
    hex += "ab";
  }
  const auto sha256 = [](std::optional<std::string> digest) {
    const std::optional<AvailableUpdate> update = Available(
        Release("v1.0.0", {"Pane-1.0.0-arm64.dmg"}, kGitHubDownloads, digest));
    EXPECT_TRUE(update);
    return update ? update->sha256 : std::nullopt;
  };

  EXPECT_EQ(sha256(base::StrCat({"sha256:", hex})), hex);
  EXPECT_EQ(sha256(base::StrCat({"sha256:", base::ToUpperASCII(hex)})), hex);
  EXPECT_EQ(sha256(std::nullopt), std::nullopt);
  EXPECT_EQ(Sha256FromDigest(base::StrCat({"sha512:", hex})), std::nullopt);
  EXPECT_EQ(Sha256FromDigest(
                base::StrCat({"sha256:", std::string_view(hex).substr(0, 63)})),
            std::nullopt);
  EXPECT_EQ(Sha256FromDigest(base::StrCat(
                {"sha256:", std::string_view(hex).substr(0, 62), "zz"})),
            std::nullopt);
}

// A tag without its v counts too.
TEST(UpdatesTest, BareTag) {
  EXPECT_EQ(Available(Release("1.0.0", {"Pane-1.0.0-arm64.dmg"}))
                .value_or(AvailableUpdate())
                .version,
            "1.0.0");
}

// JSON that is not a release, or a version that does not parse, fails the
// check.
TEST(UpdatesTest, Failures) {
  const auto not_a_release =
      AvailableUpdateIn(R"({"message":"rate limited"})", "0.2.4");
  ASSERT_FALSE(not_a_release.has_value());
  EXPECT_EQ(not_a_release.error(), UpdateCheckError::kNotARelease);

  const auto invalid_version =
      AvailableUpdateIn(Release("vnext", {"Pane-next-arm64.dmg"}), "0.2.4");
  ASSERT_FALSE(invalid_version.has_value());
  EXPECT_EQ(invalid_version.error(), UpdateCheckError::kInvalidVersion);
}

// The time left is a coarse bucket (format-eta.ts's buckets).
TEST(UpdatesTest, Eta) {
  EXPECT_EQ(FormatETA(0), "<1min");
  EXPECT_EQ(FormatETA(44), "<1min");
  EXPECT_EQ(FormatETA(45), "~1min");
  EXPECT_EQ(FormatETA(150), "~3min");
  EXPECT_EQ(FormatETA(3600), "~60min");
}

constexpr base::Time At(double seconds_since_epoch) {
  return base::Time::FromSecondsSinceUnixEpoch(seconds_since_epoch);
}

constexpr int64_t kMegabyte = 1'000'000;

// Ported from PaneKit's DownloadETATests.

constexpr base::Time kStart = At(1'800'000'000);

// Uses the average rate since the first bytes.
TEST(DownloadETATest, Average) {
  DownloadETA eta;

  // The first bytes, after the connection's setup, start the clock.
  EXPECT_EQ(eta.Update(1'000'000, 100'000'000, kStart), std::nullopt);
  // 10 MB more in 10s: 1 MB/s, 89s left.
  EXPECT_EQ(eta.Update(11'000'000, 100'000'000, kStart + base::Seconds(10)),
            89);
}

// None in the first second, however slow the start.
TEST(DownloadETATest, Settles) {
  DownloadETA eta;

  EXPECT_EQ(eta.Update(16'384, 163'000'000, kStart), std::nullopt);
  EXPECT_EQ(eta.Update(32'768, 163'000'000, kStart + base::Seconds(0.5)),
            std::nullopt);
  EXPECT_EQ(eta.Update(15'032'768, 163'000'000, kStart + base::Seconds(1)), 10);
}

// A shorter estimate shows at once; a longer one only after five seconds.
TEST(DownloadETATest, Hysteresis) {
  DownloadETA eta;

  EXPECT_EQ(eta.Update(1, 100'000'001, kStart), std::nullopt);
  EXPECT_EQ(eta.Update(50'000'001, 100'000'001, kStart + base::Seconds(10)),
            10);
  // A stall: the average drops, the estimate rises, and is held.
  EXPECT_EQ(eta.Update(50'000'001, 100'000'001, kStart + base::Seconds(20)),
            10);
  EXPECT_EQ(eta.Update(50'000'001, 100'000'001, kStart + base::Seconds(24)),
            10);
  EXPECT_EQ(eta.Update(50'000'001, 100'000'001, kStart + base::Seconds(26)),
            26);
  // Faster again: shorter at once.
  EXPECT_EQ(eta.Update(90'000'001, 100'000'001, kStart + base::Seconds(27)), 3);
}

// No estimate before the size or the rate is known.
TEST(DownloadETATest, Unknown) {
  DownloadETA eta;

  EXPECT_EQ(eta.Update(1000, 0, kStart + base::Seconds(1)), std::nullopt);
  EXPECT_EQ(eta.Update(0, 1000, kStart + base::Seconds(1)), std::nullopt);
  EXPECT_EQ(eta.Update(500, 1000, kStart + base::Seconds(2)), std::nullopt);
  EXPECT_EQ(eta.Update(500, 1000, kStart + base::Seconds(4)), std::nullopt);
}

// Ported from PaneKit's DownloadETAAcrossConnectionsTests.

// A new connection's rate replaces the last one's, and the time left holds
// until it is known.
TEST(DownloadETAAcrossConnectionsTest, NewConnection) {
  const base::Time start = At(1000);
  DownloadETA eta;
  const int64_t total = 135 * kMegabyte;
  eta.Update(1, total, start);
  const std::optional<int> crawling =
      eta.Update(kMegabyte, total, start + base::Seconds(5));
  EXPECT_EQ(crawling, 670);

  eta.RestartRate();
  const std::optional<int> held =
      eta.Update(kMegabyte, total, start + base::Seconds(5));
  EXPECT_EQ(held, crawling);

  // A second at 30 MB/s: 104 MB left, about 3.5 s, not the 20 of the average
  // since the first bytes.
  const std::optional<int> after_switch =
      eta.Update(31 * kMegabyte, total, start + base::Seconds(6));
  EXPECT_EQ(after_switch, 3);
}

// Ported from PaneKit's DownloadPaceTests.

// A connection at a good rate stays.
TEST(DownloadPaceTest, GoodRate) {
  const base::Time start = At(1000);
  DownloadPace pace(start);
  EXPECT_EQ(pace.window(), base::Seconds(5));

  const bool leaves =
      pace.ShouldSwitch(6 * kMegabyte, start + base::Seconds(5));
  EXPECT_FALSE(leaves);
  EXPECT_EQ(pace.window(), base::Seconds(5));
}

// A slow connection, or a stalled one, is left at the end of its window.
TEST(DownloadPaceTest, Slow) {
  const base::Time start = At(1000);
  DownloadPace crawling(start);
  EXPECT_TRUE(crawling.ShouldSwitch(4 * kMegabyte, start + base::Seconds(5)));

  DownloadPace stalled(start);
  EXPECT_TRUE(stalled.ShouldSwitch(0, start + base::Seconds(5)));
}

// Each new connection no faster doubles the next window, and after four the
// download stays.
TEST(DownloadPaceTest, GivesUp) {
  base::Time now = At(1000);
  DownloadPace pace(now);

  for (int window : {5, 10, 20, 40}) {
    EXPECT_EQ(pace.window(), base::Seconds(window));
    now += base::Seconds(window);
    EXPECT_TRUE(pace.ShouldSwitch(0, now));
  }

  EXPECT_EQ(pace.window(), base::Seconds(80));
  now += base::Seconds(80);
  EXPECT_FALSE(pace.ShouldSwitch(0, now));
}

// A connection that cannot be left spends no switch, and its next window is
// no longer.
TEST(DownloadPaceTest, CannotBeLeft) {
  base::Time now = At(1000);
  DownloadPace pace(now);

  for (int index = 0; index < 6; ++index) {
    now += base::Seconds(5);
    EXPECT_FALSE(pace.ShouldSwitch(0, now, /*can_leave=*/false));
    EXPECT_EQ(pace.window(), base::Seconds(5));
  }

  now += base::Seconds(5);
  EXPECT_TRUE(pace.ShouldSwitch(0, now));
  EXPECT_EQ(pace.window(), base::Seconds(10));
}

// A window begun again at a connection's first bytes runs its length from
// there.
TEST(DownloadPaceTest, FirstBytes) {
  const base::Time start = At(1000);
  DownloadPace pace(start);
  EXPECT_EQ(pace.TimeLeft(start + base::Seconds(5)), base::TimeDelta());

  pace.Begin(0, start + base::Seconds(4.5));
  EXPECT_EQ(pace.TimeLeft(start + base::Seconds(5)), base::Seconds(4.5));
}

// A connection that picks up starts the count over.
TEST(DownloadPaceTest, PicksUp) {
  const base::Time start = At(1000);
  DownloadPace pace(start);
  EXPECT_TRUE(pace.ShouldSwitch(0, start + base::Seconds(5)));
  EXPECT_EQ(pace.window(), base::Seconds(10));

  EXPECT_FALSE(pace.ShouldSwitch(100 * kMegabyte, start + base::Seconds(15)));
  EXPECT_EQ(pace.window(), base::Seconds(5));
}

// A resumed connection is judged by what it brought, not what came before.
TEST(DownloadPaceTest, Resumed) {
  const base::Time start = At(1000);
  DownloadPace pace(start);
  pace.Begin(100 * kMegabyte, start + base::Seconds(1));

  EXPECT_TRUE(pace.ShouldSwitch(101 * kMegabyte, start + base::Seconds(6)));
}

// DmgDownload's rules, which the Swift kept in the download itself.

// The rest comes from where a connection got to only where its server takes
// ranges and names the file's version, and has not refused before.
TEST(DownloadConnectionTest, Resumes) {
  const DownloadAnswer ranges{
      .status = 200, .offers_ranges = true, .validated = true};
  EXPECT_TRUE(CanResume(ranges, /*refused=*/false));
  EXPECT_FALSE(CanResume(ranges, /*refused=*/true));
  EXPECT_FALSE(CanResume({.status = 200, .offers_ranges = true},
                         /*refused=*/false));
  EXPECT_FALSE(
      CanResume({.status = 200, .validated = true}, /*refused=*/false));
}

// A slow connection that cannot resume is left only while it has brought
// nothing, or under a twentieth of the file; a resumed one that has not
// answered yet gives up nothing.
TEST(DownloadConnectionTest, Leaves) {
  const DownloadAnswer plain{.status = 200};
  const DownloadAnswer ranges{
      .status = 206, .offers_ranges = true, .validated = true};
  constexpr int64_t kTotal = 40 * kMegabyte;

  EXPECT_TRUE(CanLeaveConnection(false, ranges, false, kTotal / 2, kTotal));
  EXPECT_FALSE(CanLeaveConnection(false, ranges, true, kTotal / 2, kTotal));
  EXPECT_TRUE(CanLeaveConnection(false, plain, false, 0, kTotal));
  EXPECT_TRUE(CanLeaveConnection(false, plain, false, kTotal / 20 - 1, kTotal));
  EXPECT_FALSE(CanLeaveConnection(false, plain, false, kTotal / 20, kTotal));
  // Of unknown length, far enough in to say nothing.
  EXPECT_FALSE(CanLeaveConnection(false, plain, false, kMegabyte, -1));
  EXPECT_TRUE(CanLeaveConnection(true, DownloadAnswer(), true, kTotal / 2,
                                 /*total=*/-1));
  EXPECT_FALSE(
      CanLeaveConnection(false, DownloadAnswer(), false, kTotal / 2, kTotal));
}

// The rest comes as a 206 from where it was asked; a 200 is the whole file
// again, and anything else refuses.
TEST(DownloadConnectionTest, RangeAnswers) {
  EXPECT_EQ(ReadRangeAnswer(206, 1000, 1000), RangeAnswer::kRest);
  EXPECT_EQ(ReadRangeAnswer(206, 0, 1000), RangeAnswer::kRefused);
  EXPECT_EQ(ReadRangeAnswer(206, std::nullopt, 1000), RangeAnswer::kRefused);
  EXPECT_EQ(ReadRangeAnswer(200, std::nullopt, 1000), RangeAnswer::kWhole);
  EXPECT_EQ(ReadRangeAnswer(403, std::nullopt, 1000), RangeAnswer::kRefused);
  EXPECT_EQ(ReadRangeAnswer(416, std::nullopt, 1000), RangeAnswer::kRefused);
}

// A dropped connection goes on after 2, 4, then 8 s, and no more.
TEST(DownloadConnectionTest, Drops) {
  EXPECT_EQ(DropRetryDelay(0), base::Seconds(2));
  EXPECT_EQ(DropRetryDelay(1), base::Seconds(4));
  EXPECT_EQ(DropRetryDelay(2), base::Seconds(8));
  EXPECT_EQ(DropRetryDelay(3), std::nullopt);
}

// Ported from PaneKit's UpdateInstallTests.

// Installs over a bundle Pane runs from in place, never a translocated or
// mounted one.
TEST(UpdateInstallTest, Target) {
  EXPECT_EQ(UpdateInstallTarget("/Applications/Pane.app"),
            "/Applications/Pane.app");
  EXPECT_EQ(UpdateInstallTarget("/Users/me/Applications/Pane.app"),
            "/Users/me/Applications/Pane.app");
  EXPECT_EQ(UpdateInstallTarget(
                "/private/var/folders/x/T/AppTranslocation/1234/d/Pane.app"),
            std::nullopt);
  EXPECT_EQ(UpdateInstallTarget("/Volumes/Pane/Pane.app"), std::nullopt);
  EXPECT_EQ(UpdateInstallTarget("/usr/local/bin/pane"), std::nullopt);
}

// Stages the new version hidden beside the app, and knows its leftovers.
TEST(UpdateInstallTest, Staging) {
  EXPECT_EQ(StagedUpdateName("0.9.0"), ".Pane-0.9.0.app");
  EXPECT_TRUE(IsUpdateLeftover(".Pane-0.9.0.app"));
  EXPECT_FALSE(IsUpdateLeftover("Pane.app"));
  EXPECT_FALSE(IsUpdateLeftover(".Pane-notes.txt"));
}

// Mounts a dmg at a folder named for its process, and knows one whose process
// is gone.
TEST(UpdateInstallTest, Mounts) {
  const auto running = [](int32_t pid) { return pid == 42; };
  const std::string mine = UpdateMountName(42);

  EXPECT_TRUE(mine.starts_with("pane-update-42-"));
  EXPECT_FALSE(IsLeftoverUpdateMount(mine, running));
  EXPECT_TRUE(IsLeftoverUpdateMount(UpdateMountName(7), running));
  // Named before 0.13.1, without a pid.
  EXPECT_TRUE(IsLeftoverUpdateMount(
      "pane-update-19CDC9D9-4317-4BB6-B467-A974ACADFDC1", running));
  EXPECT_FALSE(IsLeftoverUpdateMount("pane-update-notes", running));
  EXPECT_FALSE(IsLeftoverUpdateMount("pane-update-7-notes", running));
  EXPECT_FALSE(
      IsLeftoverUpdateMount("xyz.roundedcorners.pane.1pSFam", running));
}

}  // namespace
}  // namespace pane::model
