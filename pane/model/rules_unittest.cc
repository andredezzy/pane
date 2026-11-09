// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "pane/model/address_input.h"
#include "pane/model/crash_loop.h"
#include "pane/model/extension_shortcut.h"
#include "pane/model/extension_text.h"
#include "pane/model/profile_form.h"
#include "pane/model/proxy_passwords.h"
#include "pane/model/reorder.h"
#include "pane/model/tab_history.h"
#include "pane/model/time_zones.h"
#include "pane/model/versions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's AddressInputTests. GURL writes a host's root path
// ("http://example.com/"), where Foundation's URL kept what was typed: the
// same address.

// Keeps web URLs as they are.
TEST(AddressInputTest, WebURL) {
  EXPECT_EQ(AddressURL("https://example.com/a?b=c").spec(),
            "https://example.com/a?b=c");
  EXPECT_EQ(AddressURL("http://example.com").spec(), "http://example.com/");
}

// Loads bare hosts over https.
TEST(AddressInputTest, BareHost) {
  EXPECT_EQ(AddressURL("example.com").spec(), "https://example.com/");
  EXPECT_EQ(AddressURL("localhost:3000").spec(), "https://localhost:3000/");
}

// Searches anything else.
TEST(AddressInputTest, Search) {
  EXPECT_EQ(AddressURL("pane browser").spec(),
            "https://www.google.com/search?q=pane%20browser");
  EXPECT_EQ(AddressURL("a.b c").spec(),
            "https://www.google.com/search?q=a.b%20c");
  // What a query can't hold as it is, which the Swift app sent unescaped.
  EXPECT_EQ(AddressURL("c++ & rust").spec(),
            "https://www.google.com/search?q=c%2B%2B%20%26%20rust");
}

// Ignores blank input.
TEST(AddressInputTest, Blank) {
  EXPECT_FALSE(AddressURL("   ").is_valid());
}

// A web address at rest is its domain and title; other text has no label.
TEST(AddressInputTest, Label) {
  EXPECT_EQ(AddressLabelFor("https://www.example.com/a", " A page "),
            (AddressLabel{.domain = "example.com", .title = "A page"}));
  EXPECT_EQ(
      AddressLabelFor("https://example.com/a", "https://example.com/a")->title,
      "");
  EXPECT_EQ(AddressLabelFor("data:text/html,a", "a"), std::nullopt);
  EXPECT_EQ(AddressLabelFor("pane browser", ""), std::nullopt);
}

// Ported from PaneKit's ExtensionSiteTests.

// A site reads without the list's path, and without its scheme when any.
TEST(ExtensionSiteTest, Title) {
  EXPECT_EQ(ExtensionSiteTitle("*://portal.northwind-studio.com/*"),
            "portal.northwind-studio.com");
  EXPECT_EQ(ExtensionSiteTitle("*://*.example.com/*"), "*.example.com");
  EXPECT_EQ(ExtensionSiteTitle("https://example.com/*"), "https://example.com");
  EXPECT_EQ(ExtensionSiteTitle("http://127.0.0.1:8080/*"),
            "http://127.0.0.1:8080");
}

// Ported from PaneKit's ExtensionPromptTextTests.

// Adding names Pane, with the name's quotes curled.
TEST(ExtensionPromptTextTest, AddingNamesPane) {
  EXPECT_EQ(ExtensionPromptTitle("Add extension", "Dark Reader",
                                 "Add \"Dark Reader\"?"),
            "Add “Dark Reader” to Pane?");
  EXPECT_EQ(ExtensionPromptAcceptTitle("Add extension"), "Add to Pane");
}

// Chrome's other prompts keep their title and button, quotes curled.
TEST(ExtensionPromptTextTest, OtherPrompts) {
  EXPECT_EQ(
      ExtensionPromptTitle("Accept permissions", "uBlock Origin Lite",
                           "\"uBlock Origin Lite\" needs new permissions"),
      "“uBlock Origin Lite” needs new permissions");
  EXPECT_EQ(ExtensionPromptAcceptTitle("Re-enable"), "Re-enable");
}

// Curls only the first and last straight quotes, and only a pair.
TEST(ExtensionPromptTextTest, CurlyQuotes) {
  EXPECT_EQ(CurlyQuoted("Add \"A \"B\" C\"?"), "Add “A \"B\" C”?");
  EXPECT_EQ(CurlyQuoted("Add \"A?"), "Add \"A?");
  EXPECT_EQ(CurlyQuoted("No quotes"), "No quotes");
}

// Removing asks with the name, and reports abuse on the Web Store.
TEST(ExtensionPromptTextTest, Removing) {
  EXPECT_EQ(ExtensionRemoveTitle("Dark Reader"), "Remove “Dark Reader”?");
  EXPECT_EQ(ReportAbuseURL("abc").spec(),
            "https://chrome.google.com/webstore/report/"
            "abc?utm_source=chrome-remove-extension-dialog");
}

// Ported from PaneKit's CrashLoopTests.

struct Crash {
  std::string tab;
  double seconds;
};

// Whether each crash, `seconds` after the start, is a loop; `forgotten` is
// forgotten after the crash at `index`.
std::vector<bool> Loops(const std::vector<Crash>& crashes,
                        const std::string& forgotten = "",
                        int index = -1) {
  const base::Time start = base::Time::FromSecondsSinceUnixEpoch(1'800'000'000);
  CrashLoop loop;
  std::vector<bool> loops;
  for (int position = 0; position < static_cast<int>(crashes.size());
       ++position) {
    const Crash& crash = crashes[position];
    loops.push_back(
        loop.Crashed(crash.tab, start + base::Seconds(crash.seconds)));
    if (position == index && !forgotten.empty()) {
      loop.Forget(forgotten);
    }
  }
  return loops;
}

// A first crash reloads; a second within a minute is a loop.
TEST(CrashLoopTest, SecondWithinMinute) {
  // Another tab's crash is its own.
  EXPECT_EQ(Loops({{"a", 0}, {"a", 59}, {"b", 59}}),
            (std::vector<bool>{false, true, false}));
}

// A crash a minute or more after the last reloads again.
TEST(CrashLoopTest, AfterMinute) {
  EXPECT_EQ(Loops({{"a", 0}, {"a", 60}, {"a", 61}}),
            (std::vector<bool>{false, false, true}));
}

// A forgotten tab starts over.
TEST(CrashLoopTest, Forgets) {
  EXPECT_EQ(Loops({{"a", 0}, {"a", 1}}, "a", 0),
            (std::vector<bool>{false, false}));
}

// Ported from PaneKit's ReorderTests: four collapsed profiles, 36 tall, 4
// apart, from 0.

std::vector<std::string> Ids() {
  return {"a", "b", "c", "d"};
}

int Target(const std::string& dragged,
           double top,
           std::map<std::string, double> heights = {{"a", 36},
                                                    {"b", 36},
                                                    {"c", 36},
                                                    {"d", 36}}) {
  return ReorderTarget(Ids(), heights, 0, dragged, top, 4);
}

// Drops at the place whose top is nearest the copy's.
TEST(ReorderTest, NearestPlace) {
  EXPECT_EQ(Target("a", 0), 0);
  EXPECT_EQ(Target("a", 19), 0);
  EXPECT_EQ(Target("a", 21), 1);
  EXPECT_EQ(Target("a", 86), 2);
  EXPECT_EQ(Target("a", 120), 3);
  EXPECT_EQ(Target("d", 50), 1);
}

// Drops first or last past the list's ends.
TEST(ReorderTest, Ends) {
  EXPECT_EQ(Target("d", -400), 0);
  EXPECT_EQ(Target("a", 4'000), 3);
}

// Counts an expanded profile's whole height.
TEST(ReorderTest, Expanded) {
  // b expanded: its header and three tabs, 36 + 3 × 36.
  const std::map<std::string, double> tall = {
      {"a", 36}, {"b", 144}, {"c", 36}, {"d", 36}};
  // With a out, the places' tops are 0, 148 (after b), and 188 (after c).
  EXPECT_EQ(Target("a", 140, tall), 1);
  EXPECT_EQ(Target("a", 160, tall), 1);
  EXPECT_EQ(Target("a", 170, tall), 2);
}

// Moves a row to its target among the others.
TEST(ReorderTest, Order) {
  EXPECT_EQ(ReorderedIds(Ids(), "a", 2),
            (std::vector<std::string>{"b", "c", "a", "d"}));
  EXPECT_EQ(ReorderedIds(Ids(), "d", 0),
            (std::vector<std::string>{"d", "a", "b", "c"}));
  EXPECT_EQ(ReorderedIds(Ids(), "b", 9),
            (std::vector<std::string>{"a", "c", "d", "b"}));
  EXPECT_EQ(ReorderedIds(Ids(), "z", 0), Ids());
}

// Ported from PaneKit's MostRecentTabTests (the Electron app's mru.test.ts).

std::vector<std::string> Tabs() {
  return {"a", "b", "c"};
}

// Returns the most recently used tab still present.
TEST(MostRecentTabTest, MostRecentPresent) {
  // b is more recent than c in the history, and both still exist.
  EXPECT_EQ(MostRecentTab(Tabs(), {"x", "b", "c", "a"}), "b");
}

// Skips history ids that are no longer present.
TEST(MostRecentTabTest, SkipsGone) {
  EXPECT_EQ(MostRecentTab(Tabs(), {"gone", "c", "a"}), "c");
}

// Falls back to the last tab when none are in the history.
TEST(MostRecentTabTest, FallsBackToLast) {
  EXPECT_EQ(MostRecentTab(Tabs(), {}), "c");
  EXPECT_EQ(MostRecentTab(Tabs(), {"nope"}), "c");
}

// Returns nothing when there are no tabs.
TEST(MostRecentTabTest, EmptyTabs) {
  EXPECT_EQ(MostRecentTab({}, {"a"}), std::nullopt);
}

// Ported from PaneKit's VersionsTests (the Electron app's
// compare-versions.test.ts).

// Is true when the candidate's patch is newer.
TEST(VersionsTest, NewerPatch) {
  EXPECT_EQ(IsNewerVersion("0.1.2", "0.1.1"), true);
}

// Is true when the candidate's minor or major is newer.
TEST(VersionsTest, NewerMinorOrMajor) {
  EXPECT_EQ(IsNewerVersion("0.2.0", "0.1.9"), true);
  EXPECT_EQ(IsNewerVersion("1.0.0", "0.9.9"), true);
}

// Is false when the versions are equal.
TEST(VersionsTest, Equal) {
  EXPECT_EQ(IsNewerVersion("0.1.1", "0.1.1"), false);
}

// Is false when the candidate is older.
TEST(VersionsTest, Older) {
  EXPECT_EQ(IsNewerVersion("0.1.0", "0.1.1"), false);
}

// Strips a leading v from GitHub-style release tags.
TEST(VersionsTest, LeadingV) {
  EXPECT_EQ(IsNewerVersion("v0.1.2", "0.1.1"), true);
  EXPECT_EQ(IsNewerVersion("v0.1.1", "v0.1.1"), false);
}

// Treats missing trailing segments as zero.
TEST(VersionsTest, MissingSegments) {
  EXPECT_EQ(IsNewerVersion("0.2", "0.1.9"), true);
  EXPECT_EQ(IsNewerVersion("1", "1.0.0"), false);
}

// Fails on a malformed version segment.
TEST(VersionsTest, Malformed) {
  EXPECT_FALSE(IsNewerVersion("0.x.1", "0.1.0").has_value());
}

// Ported from PaneKit's TabHistoryTests.

// Moves a used tab to the front once.
TEST(TabHistoryTest, Use) {
  TabHistory history;
  history.Use("a");
  history.Use("b");
  history.Use("a");

  EXPECT_EQ(history.mru(), (std::vector<std::string>{"a", "b"}));
}

// Forgets a closed tab.
TEST(TabHistoryTest, Forget) {
  TabHistory history;
  history.Use("a");
  history.Use("b");
  history.Forget("a");

  EXPECT_EQ(history.mru(), (std::vector<std::string>{"b"}));
}

// Keeps the 20 most recently closed tabs, newest first.
TEST(TabHistoryTest, ClosedStack) {
  TabHistory history;
  for (int index = 0; index < 25; ++index) {
    history.PushClosed(
        {.url = "https://" + base::NumberToString(index) + ".test/",
         .profile_id = "p"});
  }

  EXPECT_EQ(history.closed().size(), 20u);
  EXPECT_EQ(history.PopClosed()->url, "https://24.test/");
  EXPECT_EQ(history.closed().size(), 19u);
}

// Pops nothing from an empty stack.
TEST(TabHistoryTest, EmptyPop) {
  TabHistory history;

  EXPECT_EQ(history.PopClosed(), std::nullopt);
}

// Ported from PaneKit's ExtensionShortcutTests.

using enum KeyModifier;

ShortcutRefusal Refusal(ShortcutRefusal::Reason reason) {
  return {.reason = reason};
}

// Chrome's shortcut reads in the Mac's order, the key last.
TEST(ExtensionShortcutsTest, Glyphs) {
  EXPECT_EQ(ShortcutGlyphs("Command+Shift+Y"), "⇧⌘Y");
  EXPECT_EQ(ShortcutGlyphs("Ctrl+Alt+Shift+Command+K"), "⌃⌥⇧⌘K");
  EXPECT_EQ(ShortcutGlyphs("Alt+Shift+Comma"), "⌥⇧,");
  EXPECT_EQ(ShortcutGlyphs("Ctrl+Up"), "⌃↑");
  EXPECT_EQ(ShortcutGlyphs("Command+Space"), "⌘Space");
  EXPECT_EQ(ShortcutGlyphs("Command+7"), "⌘7");
  EXPECT_EQ(ShortcutGlyphs(""), "");
}

// Held modifiers read in the same order.
TEST(ExtensionShortcutsTest, HeldModifiers) {
  EXPECT_EQ(ModifierGlyphs({kCommand, kOption}), "⌥⌘");
  EXPECT_EQ(ModifierGlyphs({kShift, kControl, kCapsLock}), "⌃⇧");
  EXPECT_EQ(ModifierGlyphs({}), "");
}

// A key alone needs Ctrl, Alt, or ⌘; Shift is not one.
TEST(ExtensionShortcutsTest, IncludeModifier) {
  const ShortcutRefusal include =
      Refusal(ShortcutRefusal::Reason::kIncludeModifier);

  EXPECT_EQ(ShortcutRefusalFor({}), include);
  EXPECT_EQ(ShortcutRefusalFor({kShift}), include);
  EXPECT_EQ(ShortcutRefusalFor({kCapsLock}), include);
  EXPECT_EQ(ShortcutRefusalFor({kCommand, kShift}), std::nullopt);
  EXPECT_EQ(ShortcutRefusalFor({kControl}), std::nullopt);
  EXPECT_EQ(ShortcutRefusalFor({kOption, kShift}), std::nullopt);
  EXPECT_EQ(ShortcutRefusalFor({kControl, kCommand}), std::nullopt);
}

// ⌥ with ⌃ or ⌘, or three modifiers, is an invalid combination.
TEST(ExtensionShortcutsTest, InvalidCombination) {
  const ShortcutRefusal invalid =
      Refusal(ShortcutRefusal::Reason::kInvalidCombination);

  EXPECT_EQ(ShortcutRefusalFor({kOption, kCommand}), invalid);
  EXPECT_EQ(ShortcutRefusalFor({kOption, kControl}), invalid);
  EXPECT_EQ(ShortcutRefusalFor({kControl, kOption, kShift, kCommand}), invalid);
  EXPECT_EQ(ShortcutRefusalFor({kControl, kShift, kCommand}), invalid);
}

// The messages are Chrome's macOS strings, and Pane's for its own keys.
TEST(ExtensionShortcutsTest, Messages) {
  EXPECT_EQ(Refusal(ShortcutRefusal::Reason::kIncludeModifier).Message(),
            "Include Ctrl, Alt, or ⌘");
  EXPECT_EQ(Refusal(ShortcutRefusal::Reason::kInvalidCombination).Message(),
            "Invalid combination");
  EXPECT_EQ(Refusal(ShortcutRefusal::Reason::kTypeALetter).Message(),
            "Type a letter");
  EXPECT_EQ((ShortcutRefusal{.reason = ShortcutRefusal::Reason::kPanes,
                             .item = "New Tab",
                             .shortcut = "Command+T"})
                .Message(),
            "⌘T is Pane’s New Tab");
}

// Ported from PaneKit's TimeZonesTests. Its known zones are ICU's, by their
// IANA names: Foundation's list had a few older names beside or instead of
// them (Asia/Calcutta for Asia/Kolkata, Europe/Kiev beside Europe/Kyiv).

// 15 January 2027: New York on standard time.
constexpr base::Time kJanuary =
    base::Time::FromSecondsSinceUnixEpoch(1'800'000'000);

std::vector<std::string> RegionNames(
    const std::vector<TimeZoneRegion>& regions) {
  std::vector<std::string> names;
  for (const TimeZoneRegion& region : regions) {
    names.push_back(region.name);
  }
  return names;
}

std::vector<std::string> Titles(const TimeZoneRegion& region) {
  std::vector<std::string> titles;
  for (const TimeZoneChoice& choice : region.choices) {
    titles.push_back(choice.title);
  }
  return titles;
}

// The regions, in order, leave out what nobody would claim.
TEST(TimeZonesTest, Regions) {
  const std::vector<TimeZoneRegion> regions =
      TimeZoneRegions({"Antarctica/Troll", "Arctic/Longyearbyen", "GMT",
                       "Pacific/Fiji", "Africa/Lagos"},
                      base::Time::Now());

  EXPECT_EQ(RegionNames(regions),
            (std::vector<std::string>{"Africa", "Pacific"}));
}

// Each choice names its place and its offset, sorted within its region.
TEST(TimeZonesTest, Choices) {
  const std::vector<TimeZoneRegion> regions =
      TimeZoneRegions({"America/New_York", "America/Argentina/Buenos_Aires",
                       "Asia/Kolkata", "Asia/Tokyo"},
                      kJanuary);
  ASSERT_EQ(RegionNames(regions),
            (std::vector<std::string>{"America", "Asia"}));
  const TimeZoneRegion& america = regions[0];
  const TimeZoneRegion& asia = regions[1];

  EXPECT_EQ(Titles(america),
            (std::vector<std::string>{"Argentina, Buenos Aires (GMT−3)",
                                      "New York (GMT−5)"}));
  EXPECT_EQ(america.choices.back().identifier, "America/New_York");
  EXPECT_EQ(Titles(asia),
            (std::vector<std::string>{"Kolkata (GMT+5:30)", "Tokyo (GMT+9)"}));
}

// A zone's city, for the trigger and the note.
TEST(TimeZonesTest, City) {
  EXPECT_EQ(TimeZoneCity("America/Sao_Paulo"), "Sao Paulo");
  EXPECT_EQ(TimeZoneCity("America/Argentina/Buenos_Aires"), "Buenos Aires");
  EXPECT_EQ(TimeZoneCity(kUTCTimeZone), "UTC");
}

// Every region lists what this Mac knows.
TEST(TimeZonesTest, Known) {
  const std::vector<TimeZoneRegion> regions = TimeZoneRegions();

  EXPECT_EQ(
      RegionNames(regions),
      (std::vector<std::string>{"Africa", "America", "Asia", "Atlantic",
                                "Australia", "Europe", "Indian", "Pacific"}));
  EXPECT_TRUE(std::ranges::any_of(regions, [](const TimeZoneRegion& region) {
    return std::ranges::any_of(region.choices,
                               [](const TimeZoneChoice& choice) {
                                 return choice.identifier == "America/New_York";
                               });
  }));
}

// Ported from PaneKit's ProxyPasswordsTests.

BrowserProfile ProxiedProfile(
    const std::string& id,
    std::optional<std::string> username = std::nullopt,
    std::optional<std::string> password = std::nullopt,
    bool proxied = true) {
  BrowserProfile profile;
  profile.id = id;
  profile.name = id;
  if (proxied) {
    profile.proxy.emplace();
    profile.proxy->type = ProxyType::kHttp;
    profile.proxy->host = "proxy.example";
    profile.proxy->port = 3128;
    profile.proxy->username = std::move(username);
    profile.proxy->password = std::move(password);
  }
  profile.created_at = "2026-10-27T00:00:00.000Z";
  profile.updated_at = "2026-10-27T00:00:00.000Z";
  return profile;
}

std::optional<std::string> PasswordOf(const BrowserProfile& profile) {
  return profile.proxy ? profile.proxy->password : std::nullopt;
}

// The passwords an earlier Pane kept apart go back into its profiles.
TEST(ProxyPasswordsTest, Filled) {
  const std::vector<BrowserProfile> profiles = FillProxyPasswords(
      {ProxiedProfile("a", "u"), ProxiedProfile("b", "u"),
       ProxiedProfile("c", std::nullopt, std::nullopt, /*proxied=*/false)},
      {{"a", "s3cret"}, {"c", "no proxy"}});

  ASSERT_EQ(profiles.size(), 3u);
  EXPECT_EQ(PasswordOf(profiles[0]), "s3cret");
  EXPECT_EQ(PasswordOf(profiles[1]), std::nullopt);
  EXPECT_EQ(PasswordOf(profiles[2]), std::nullopt);
}

// A password the file still holds is read, and wins.
TEST(ProxyPasswordsTest, Legacy) {
  const std::vector<BrowserProfile> legacy = {
      ProxiedProfile("a", "u", "from-file")};

  EXPECT_TRUE(NeedsProxyPasswordStore(legacy));
  EXPECT_EQ(PasswordOf(FillProxyPasswords(legacy, {{"a", "stale"}}).front()),
            "from-file");
}

// Only a proxy that signs in needs the store.
TEST(ProxyPasswordsTest, NeedsStore) {
  EXPECT_FALSE(NeedsProxyPasswordStore(
      {ProxiedProfile("a"),
       ProxiedProfile("b", std::nullopt, std::nullopt, /*proxied=*/false)}));
  EXPECT_TRUE(NeedsProxyPasswordStore({ProxiedProfile("a", "u")}));
}

// Ported from PaneKit's ProfileFormTests: its presets, which the engine's
// fingerprints start from.

// Each platform's preset is the Electron app's.
TEST(ProfileFormTest, Presets) {
  EXPECT_EQ(
      FingerprintPreset(Platform::kWindows).screen,
      (Fingerprint::Screen{.width = 1920, .height = 1080, .color_depth = 24}));
  EXPECT_EQ(
      FingerprintPreset(Platform::kMacOS).screen,
      (Fingerprint::Screen{.width = 1440, .height = 900, .color_depth = 30}));
  EXPECT_EQ(FingerprintPreset(Platform::kLinux).hardware_concurrency, 4);
  for (Platform platform :
       {Platform::kWindows, Platform::kMacOS, Platform::kLinux}) {
    EXPECT_EQ(FingerprintPreset(platform).platform, platform);
  }
}

}  // namespace
}  // namespace pane::model
