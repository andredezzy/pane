// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/profile_draft.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "pane/model/profile_form.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's ProfileDraftTests.

BrowserProfile MakeProfile(
    std::optional<Fingerprint> fingerprint = std::nullopt,
    std::optional<ProxyConfig> proxy = std::nullopt) {
  BrowserProfile profile;
  profile.id = "p";
  profile.name = "Work";
  profile.color = ProfileColor::kEmerald;
  profile.group = "Clients";
  profile.fingerprint = std::move(fingerprint);
  profile.proxy = std::move(proxy);
  return profile;
}

ProxyConfig MakeProxy(ProxyType type,
                      std::string host,
                      int port,
                      std::optional<std::string> username = std::nullopt,
                      std::optional<std::string> password = std::nullopt) {
  ProxyConfig proxy;
  proxy.type = type;
  proxy.host = std::move(host);
  proxy.port = port;
  proxy.username = std::move(username);
  proxy.password = std::move(password);
  return proxy;
}

// A new profile's form: spoofing on the host's platform, no proxy.
TEST(ProfileDraftTest, NewForm) {
  const ProfileDraft draft;

  EXPECT_TRUE(draft.name.empty());
  EXPECT_TRUE(draft.saves_history);
  EXPECT_TRUE(draft.spoofing);
  EXPECT_EQ(draft.platform, HostPlatform());
  EXPECT_FALSE(draft.proxy_enabled);
  EXPECT_EQ(draft.proxy_port, "8080");
}

// Saving an edit untouched leaves the profile as it was.
TEST(ProfileDraftTest, UntouchedEdit) {
  const BrowserProfile original = MakeProfile(
      FingerprintPreset(Platform::kWindows),
      MakeProxy(ProxyType::kSocks5, "proxy.example", 1080, "user", "p:ss"));
  BrowserProfile saved = original;

  ProfileDraft(original).ApplyTo(saved, [] {
    ADD_FAILURE() << "regenerated the fingerprint";
    return FingerprintPreset(Platform::kMacOS);
  });

  EXPECT_EQ(saved, original);
}

// An existing profile fills the form.
TEST(ProfileDraftTest, FillsForm) {
  const ProfileDraft draft(
      MakeProfile(FingerprintPreset(Platform::kLinux),
                  MakeProxy(ProxyType::kHttps, "h", 3128)));

  EXPECT_EQ(draft.name, "Work");
  EXPECT_EQ(draft.color, ProfileColor::kEmerald);
  EXPECT_TRUE(draft.spoofing);
  EXPECT_EQ(draft.platform, Platform::kLinux);
  EXPECT_TRUE(draft.proxy_enabled);
  EXPECT_EQ(draft.proxy_type, ProxyType::kHttps);
  EXPECT_EQ(draft.proxy_port, "3128");
  EXPECT_TRUE(draft.proxy_username.empty());
  EXPECT_FALSE(ProfileDraft(MakeProfile()).spoofing);
}

// The time zone goes from the profile to the form and back, and survives
// another platform.
TEST(ProfileDraftTest, TimeZone) {
  Fingerprint claimed = FingerprintPreset(Platform::kMacOS);
  claimed.timezone = "Asia/Tokyo";
  BrowserProfile saved = MakeProfile(claimed);
  const auto windows = [] { return FingerprintPreset(Platform::kWindows); };

  ProfileDraft draft(saved);
  EXPECT_EQ(draft.time_zone, "Asia/Tokyo");

  draft.platform = Platform::kWindows;
  draft.ApplyTo(saved, windows);
  ASSERT_TRUE(saved.fingerprint);
  EXPECT_EQ(saved.fingerprint->platform, Platform::kWindows);
  EXPECT_EQ(saved.fingerprint->timezone, "Asia/Tokyo");

  draft.time_zone = "";
  draft.ApplyTo(saved, windows);
  ASSERT_TRUE(saved.fingerprint);
  EXPECT_EQ(saved.fingerprint->timezone, "");

  draft.time_zone = "Asia/Tokyo";
  draft.spoofing = false;
  draft.ApplyTo(saved, windows);
  EXPECT_EQ(saved.fingerprint, std::nullopt);
  EXPECT_TRUE(ProfileDraft(saved).time_zone.empty());
}

// A new profile claims no time zone: it follows the computer.
TEST(ProfileDraftTest, NewTimeZone) {
  EXPECT_TRUE(ProfileDraft().time_zone.empty());

  for (Platform platform :
       {Platform::kWindows, Platform::kMacOS, Platform::kLinux}) {
    EXPECT_TRUE(FingerprintPreset(platform).timezone.empty());
  }
}

// Turning history off owes a clear, kept until it is done; back on owes none.
TEST(ProfileDraftTest, ClearOwed) {
  BrowserProfile saved = MakeProfile();
  EXPECT_FALSE(saved.clears_history);

  saved.SetSavesHistory(false);
  EXPECT_TRUE(saved.clears_history);

  saved.clears_history = false;
  saved.SetSavesHistory(false);
  EXPECT_FALSE(saved.clears_history);

  saved.SetSavesHistory(true);
  saved.SetSavesHistory(false);
  EXPECT_TRUE(saved.clears_history);

  saved.SetSavesHistory(true);
  EXPECT_FALSE(saved.clears_history);
}

// The history choice goes from the profile to the form and back.
TEST(ProfileDraftTest, HistoryChoice) {
  BrowserProfile saved = MakeProfile();
  saved.SetSavesHistory(false);

  ProfileDraft draft(saved);
  EXPECT_FALSE(draft.saves_history);

  draft.saves_history = true;
  draft.ApplyTo(saved, [] { return FingerprintPreset(Platform::kMacOS); });
  EXPECT_TRUE(saved.saves_history);
}

// Saving an IPv6 host leaves it and the credentials as they are.
TEST(ProfileDraftTest, Ipv6Host) {
  ProfileDraft draft;
  draft.proxy_host = "[2606:4700:4700::1111]";
  draft.proxy_port = "1080";
  draft.proxy_username = "user";

  const bool pasted = draft.PasteProxy(draft.proxy_host);
  EXPECT_FALSE(pasted);
  EXPECT_EQ(draft.proxy_host, "[2606:4700:4700::1111]");
  EXPECT_EQ(draft.proxy_port, "1080");
  EXPECT_EQ(draft.proxy_username, "user");
}

// A name is required, spaces alone are none.
TEST(ProfileDraftTest, NameRequired) {
  ProfileDraft draft;
  draft.name = "   ";

  EXPECT_EQ(draft.Validate(), ProfileDraft::Problem::kNameMissing);

  draft.name = " Work ";
  EXPECT_EQ(draft.Validate(), std::nullopt);
  EXPECT_EQ(draft.TrimmedName(), "Work");
}

// A proxy with a host saves only with a port of 1 to 65535.
TEST(ProfileDraftTest, PortChecked) {
  ProfileDraft draft;
  draft.name = "Work";
  draft.proxy_enabled = true;
  draft.proxy_host = "proxy.example";

  for (std::string_view port : {"0", "65536", "-1", "abc", ""}) {
    draft.proxy_port = std::string(port);
    EXPECT_EQ(draft.Validate(), ProfileDraft::Problem::kPortInvalid)
        << "port " << port;
    EXPECT_EQ(draft.Proxy(), std::nullopt);
  }

  draft.proxy_port = " 65535 ";
  EXPECT_EQ(draft.Validate(), std::nullopt);
  ASSERT_TRUE(draft.Proxy());
  EXPECT_EQ(draft.Proxy()->port, 65535);
}

// A missing name is the first problem, before the port.
TEST(ProfileDraftTest, NameFirst) {
  ProfileDraft draft;
  draft.proxy_enabled = true;
  draft.proxy_host = "proxy.example";
  draft.proxy_port = "0";

  EXPECT_EQ(draft.Validate(), ProfileDraft::Problem::kNameMissing);
}

// The proxy off saves as no proxy, whatever its fields hold.
TEST(ProfileDraftTest, NoProxy) {
  ProfileDraft draft;
  draft.name = "Work";
  draft.proxy_host = "http://proxy.example";
  draft.proxy_port = "0";

  EXPECT_EQ(draft.Validate(), std::nullopt);
  EXPECT_EQ(draft.Proxy(), std::nullopt);
}

// The proxy on does not save without a host, before anything about its port.
TEST(ProfileDraftTest, HostMissing) {
  ProfileDraft draft;
  draft.name = "Work";
  draft.proxy_enabled = true;
  draft.proxy_host = "  ";
  draft.proxy_port = "0";

  EXPECT_EQ(draft.Validate(), ProfileDraft::Problem::kHostMissing);

  draft.name = "";
  EXPECT_EQ(draft.Validate(), ProfileDraft::Problem::kNameMissing);
}

// The proxy on does not save with a host Chrome cannot read.
TEST(ProfileDraftTest, HostInvalid) {
  ProfileDraft draft;
  draft.name = "Work";
  draft.proxy_enabled = true;

  for (std::string_view host :
       {"http://45.12.8.19", "socks5://u:p@45.12.8.19:1080", "u:p@45.12.8.19",
        "45.12.8.19/", "not a host", "[::1", "fe80::1%en0", "999.1.1.1",
        "1.2.3", ".proxy", "a..b", "host:abc",
        // What the Mac's inet_pton took and the Swift app saved, which
        // Chrome refuses (a group of five digits, an octal 08) or reads as
        // octal (010 is 8).
        "00001::", "::1.2.3.08", "010.1.1.1", "::ffff:010.1.1.1"}) {
    draft.proxy_host = std::string(host);
    EXPECT_EQ(draft.Validate(), ProfileDraft::Problem::kHostInvalid)
        << "host " << host;
  }

  for (std::string_view host :
       {"proxy.example", "PROXY.example", "exa_mple.test", "exämple.test",
        "localhost", "45.12.8.19", "::1", "[::1]", "2001:db8::1",
        " proxy.example "}) {
    draft.proxy_host = std::string(host);
    EXPECT_EQ(draft.Validate(), std::nullopt) << "host " << host;
  }
}

// The proxy trims its host and leaves empty credentials out.
TEST(ProfileDraftTest, ProxyFields) {
  ProfileDraft draft;
  draft.proxy_enabled = true;
  draft.proxy_type = ProxyType::kSocks5;
  draft.proxy_host = " proxy.example ";
  draft.proxy_port = "1080";

  EXPECT_EQ(draft.Proxy(),
            MakeProxy(ProxyType::kSocks5, "proxy.example", 1080));

  draft.proxy_username = "user";
  draft.proxy_password = "secret";
  ASSERT_TRUE(draft.Proxy());
  EXPECT_EQ(draft.Proxy()->username, "user");
  EXPECT_EQ(draft.Proxy()->password, "secret");
}

// A pasted proxy fills its fields; one it leaves out keeps what was typed.
TEST(ProfileDraftTest, Paste) {
  ProfileDraft draft;
  draft.proxy_username = "typed";
  draft.proxy_password = "kept";

  const bool pasted = draft.PasteProxy("proxy.example:3128");
  EXPECT_TRUE(pasted);
  EXPECT_EQ(draft.proxy_host, "proxy.example");
  EXPECT_EQ(draft.proxy_port, "3128");
  EXPECT_EQ(draft.proxy_username, "typed");
  EXPECT_EQ(draft.proxy_password, "kept");

  const bool with_credentials = draft.PasteProxy("host:8080:user:p:ss");
  EXPECT_TRUE(with_credentials);
  EXPECT_EQ(draft.proxy_username, "user");
  EXPECT_EQ(draft.proxy_password, "p:ss");

  const bool bad_port = draft.PasteProxy("host:70000");
  EXPECT_FALSE(bad_port);
  EXPECT_EQ(draft.proxy_host, "host");
  EXPECT_EQ(draft.proxy_port, "8080");
}

// A new profile gets a generated fingerprint while spoofing, none without.
TEST(ProfileDraftTest, NewFingerprint) {
  ProfileDraft draft;
  draft.name = "Work";
  draft.platform = Platform::kWindows;

  BrowserProfile created;
  created.id = "p";
  draft.ApplyTo(created, [&] { return FingerprintPreset(draft.platform); });
  EXPECT_EQ(created.fingerprint, FingerprintPreset(Platform::kWindows));
  EXPECT_EQ(created.name, "Work");

  draft.spoofing = false;
  draft.ApplyTo(created, [] { return FingerprintPreset(Platform::kLinux); });
  EXPECT_EQ(created.fingerprint, std::nullopt);
}

// Changing the platform regenerates the fingerprint.
TEST(ProfileDraftTest, EditedFingerprint) {
  BrowserProfile saved = MakeProfile(FingerprintPreset(Platform::kWindows));
  ProfileDraft draft(saved);
  draft.platform = Platform::kLinux;

  draft.ApplyTo(saved, [] { return FingerprintPreset(Platform::kLinux); });

  EXPECT_EQ(saved.fingerprint, FingerprintPreset(Platform::kLinux));
}

// The sheet shows no Group: a saved one stays as it is, spaces and all, and a
// new profile has none (the user's answer, 2026-11-11).
TEST(ProfileDraftTest, GroupKept) {
  BrowserProfile saved = MakeProfile();
  saved.group = " Clients ";
  ProfileDraft draft(saved);
  draft.name = "Renamed";
  draft.color = ProfileColor::kRose;

  draft.ApplyTo(saved, [] { return FingerprintPreset(Platform::kLinux); });

  EXPECT_EQ(saved.group, " Clients ");
  EXPECT_EQ(saved.ToValue().GetDict().Find("group")->GetString(), " Clients ");

  BrowserProfile created;
  ProfileDraft().ApplyTo(created,
                         [] { return FingerprintPreset(Platform::kLinux); });
  EXPECT_EQ(created.group, std::nullopt);
}

// Each ring stays until its field can save.
TEST(ProfileDraftTest, Remaining) {
  using enum ProfileDraft::Problem;

  ProfileDraft draft;
  EXPECT_EQ(draft.Remaining(kNameMissing), kNameMissing);
  draft.name = "Work";
  EXPECT_EQ(draft.Remaining(kNameMissing), std::nullopt);

  draft.proxy_enabled = true;
  EXPECT_EQ(draft.Remaining(kHostMissing), kHostMissing);
  draft.proxy_host = "http:";
  EXPECT_EQ(draft.Remaining(kHostMissing), kHostInvalid);
  draft.proxy_host = "example.com";
  EXPECT_EQ(draft.Remaining(kHostInvalid), std::nullopt);

  draft.proxy_port = "70000";
  EXPECT_EQ(draft.Remaining(kPortInvalid), kPortInvalid);
  draft.proxy_port = "700";
  EXPECT_EQ(draft.Remaining(kPortInvalid), std::nullopt);

  draft.proxy_port = "70000";
  draft.proxy_host = "";
  draft.proxy_enabled = false;
  EXPECT_EQ(draft.Remaining(kPortInvalid), std::nullopt);
  EXPECT_EQ(draft.Remaining(kHostMissing), std::nullopt);
}

}  // namespace
}  // namespace pane::model
