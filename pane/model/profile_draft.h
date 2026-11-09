// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PROFILE_DRAFT_H_
#define PANE_MODEL_PROFILE_DRAFT_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/functional/function_ref.h"
#include "pane/model/profile.h"
#include "pane/model/profile_form.h"

namespace pane::model {

// The profile sheet's form (the Electron app's profile-sheet.tsx), as the user
// fills it in: what it starts from, what keeps it from saving, and what it
// saves. The sheet holds one; the controller applies it.
struct ProfileDraft {
  // What keeps it from saving, the first found, shown as a red ring around its
  // field, whose words are for the screen reader.
  enum class Problem {
    kNameMissing,
    kHostMissing,
    kHostInvalid,
    kPortInvalid,
  };

  // A new profile's form.
  ProfileDraft();
  // An existing profile's, as it is.
  explicit ProfileDraft(const BrowserProfile& profile);
  ProfileDraft(const ProfileDraft&);
  ProfileDraft& operator=(const ProfileDraft&);
  ~ProfileDraft();

  std::string_view TrimmedName() const;

  // The sheet says "Its traffic goes through this proxy" while the switch is
  // on: no host, or no valid port, would save as no proxy, and the profile
  // would go direct, and a host Chrome cannot read would fail its every page.
  // None of them saves.
  std::optional<Problem> Validate() const;

  // What of `problem` an edit leaves: its ring stays until its field can save,
  // so "http:" on the way to something else is still no host, and "700" on
  // the way to 7000 goes at once. The proxy's go with its switch.
  std::optional<Problem> Remaining(Problem problem) const;

  // The proxy as filled in, once it has a host and a valid port.
  std::optional<ProxyConfig> Proxy() const;

  // The paste shortcut (ProxyString) fills the proxy's fields; a credential
  // it leaves out keeps what was typed. Whether it parsed.
  bool PasteProxy(std::string_view raw);

  // What saving writes into the profile. A fingerprint is generated only when
  // spoofing turns on or the platform changes (EditedFingerprint).
  void ApplyTo(BrowserProfile& profile,
               base::FunctionRef<Fingerprint()> generate) const;

  friend bool operator==(const ProfileDraft&, const ProfileDraft&) = default;

  std::string name;
  ProfileColor color = ProfileColor::kBlue;
  std::string group;
  bool saves_history = true;
  bool spoofing = true;
  Platform platform = kHostPlatform;
  // The zone the fingerprint claims; empty, the computer's.
  std::string time_zone;
  bool proxy_enabled = false;
  ProxyType proxy_type = ProxyType::kHttp;
  std::string proxy_host;
  std::string proxy_port = "8080";
  std::string proxy_username;
  std::string proxy_password;
};

// A problem's words, for the screen reader.
std::string_view MessageOf(ProfileDraft::Problem problem);

}  // namespace pane::model

#endif  // PANE_MODEL_PROFILE_DRAFT_H_
