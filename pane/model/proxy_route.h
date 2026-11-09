// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PROXY_ROUTE_H_
#define PANE_MODEL_PROXY_ROUTE_H_

#include <optional>
#include <string>
#include <string_view>

#include "pane/model/profile.h"

namespace pane::model {

// How a profile's proxy reaches Chrome (the Electron app's proxy-relay.ts and
// profile-tabs.ts): Chrome's proxy rule for it, whether it needs the app's
// SOCKS relay, and the WebRTC policy that keeps the real address from leaking
// past it.

// Chromium cannot authenticate to a SOCKS proxy, so a SOCKS proxy with
// credentials is reached through the app's local relay, which signs in
// upstream.
bool NeedsRelay(const ProxyConfig& proxy);

// Chrome's fixed proxy rule: the proxy itself, or the local relay on
// `relay_port` when it needs one.
std::string ProxyRules(const ProxyConfig& proxy, std::optional<int> relay_port);

// Whether Chrome's authentication challenge comes from this proxy: Chrome
// names it "scheme://host:port" in its own form, whatever was typed: "[::1]"
// for "0:0:0:0:0:0:0:1", a name in lowercase and punycode, and no port where
// it is the scheme's. Missed, the user is asked for what the profile holds.
bool IsChallenger(const ProxyConfig& proxy, std::string_view challenger);

// The host for a connection of Pane's own, without an IPv6 host's brackets.
std::string ConnectionHost(const ProxyConfig& proxy);

// WebRTC's ICE candidates would leak the real LAN address past a proxy (its
// rules govern TCP only): private interfaces stay hidden, and a proxied
// profile sends all UDP through the proxy, which means none.
std::string_view WebRTCPolicyForProfile(bool proxied);

}  // namespace pane::model

#endif  // PANE_MODEL_PROXY_ROUTE_H_
