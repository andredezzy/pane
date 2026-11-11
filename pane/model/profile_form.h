// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PROFILE_FORM_H_
#define PANE_MODEL_PROFILE_FORM_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/functional/function_ref.h"
#include "pane/model/host_platform.h"
#include "pane/model/profile.h"

namespace pane::model {

// The profile sheet's rules (the Electron app's profile-sheet.tsx and
// default-fingerprints.ts): the fingerprint a platform gets, the proxy string
// shortcut, and when an edit keeps the stored fingerprint.

// The fingerprint each platform starts from, default-fingerprints.ts's. The
// engine replaces the Chrome version it claims with its own.
Fingerprint FingerprintPreset(Platform platform);

// The fingerprint a new profile on `platform` gets. One on the host's own
// platform claims the host's real GPU, so its WebGL claim matches what the
// hardware draws (Cloudflare Turnstile flags a spoofed renderer); a cross-OS
// one keeps the preset's, which no claim can make consistent.
Fingerprint GeneratedFingerprint(
    Platform platform,
    const std::optional<Fingerprint::WebGL>& host_webgl);

// What an edit saves: the stored fingerprint, unless the user changed what
// generates it (spoofing on or off, or the platform). Regenerating on a
// rename would re-pin the GPU to the current host and pick up preset changes,
// changing what sites see of the profile.
std::optional<Fingerprint> EditedFingerprint(
    const std::optional<Fingerprint>& stored,
    bool spoofing,
    Platform platform,
    base::FunctionRef<Fingerprint()> generate);

// The proxy field's shortcut: "host:port", "host:port:user", or
// "host:port:user:password", as proxy sellers list them. The password is the
// rest of the line, colons and all. An IPv6 host takes its brackets, and an
// IPv6 address alone is a host, not a shortcut: the sheet reads what is in the
// host field as one.
struct ProxyString {
  ProxyString();
  ProxyString(const ProxyString&);
  ProxyString& operator=(const ProxyString&);
  ~ProxyString();

  // nullopt for anything else.
  static std::optional<ProxyString> Parse(std::string_view raw);

  friend bool operator==(const ProxyString&, const ProxyString&) = default;

  std::string host;
  int port = 0;
  std::optional<std::string> username;
  std::optional<std::string> password;
};

// Whether Chrome can read this as a proxy's host: a name, an IPv4 address, or
// an IPv6 one with its brackets or without. A scheme, credentials, a path, or
// spaces make a rule Chrome drops, which fails the profile's every request.
bool IsProxyHost(std::string_view text);

// The ports a proxy can listen on: TCP's, without 0.
bool IsProxyPort(int port);

// The port typed or pasted, or nullopt when it is not one a proxy can listen
// on: the SOCKS relay would trap on it, and Chrome would fail every request
// of the profile.
std::optional<int> ProxyPort(std::string_view text);

// A field as the sheet reads it: without the tabs and spaces around it,
// Unicode's among them (Swift's CharacterSet.whitespaces). Line breaks stay.
std::string_view TrimSpaces(std::string_view text);

}  // namespace pane::model

#endif  // PANE_MODEL_PROFILE_FORM_H_
