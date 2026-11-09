// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/proxy_route.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace pane::model {

namespace {

std::string_view SchemeOf(ProxyType type) {
  switch (type) {
    case ProxyType::kHttp:
      return "http";
    case ProxyType::kHttps:
      return "https";
    case ProxyType::kSocks4:
      return "socks4";
    case ProxyType::kSocks5:
      return "socks5";
  }
}

std::string_view WithoutBrackets(std::string_view host) {
  if (host.starts_with('[') && host.ends_with(']')) {
    host.remove_prefix(1);
    host.remove_suffix(1);
  }
  return host;
}

// An IPv6 host, as typed with its brackets or without. Chrome cannot read a
// bare one's colons, and fails every request of the profile.
std::string ChromeHost(const ProxyConfig& proxy) {
  return proxy.host.find(':') != std::string::npos &&
                 !proxy.host.starts_with('[')
             ? base::StrCat({"[", proxy.host, "]"})
             : proxy.host;
}

// A host as Chrome writes it: an IPv6 address compressed, a name in lowercase
// and punycode; without brackets.
std::string CanonicalHost(std::string_view host) {
  const std::string_view bare = WithoutBrackets(host);
  const GURL url(bare.find(':') != std::string_view::npos
                     ? base::StrCat({"http://[", bare, "]/"})
                     : base::StrCat({"http://", bare, "/"}));
  return url.is_valid() && url.has_host()
             ? std::string(WithoutBrackets(url.host()))
             : base::ToLowerASCII(bare);
}

}  // namespace

bool NeedsRelay(const ProxyConfig& proxy) {
  return (proxy.type == ProxyType::kSocks4 ||
          proxy.type == ProxyType::kSocks5) &&
         !proxy.username.value_or("").empty();
}

std::string ProxyRules(const ProxyConfig& proxy,
                       std::optional<int> relay_port) {
  if (NeedsRelay(proxy) && relay_port) {
    return base::StrCat(
        {"socks5://127.0.0.1:", base::NumberToString(*relay_port)});
  }
  return base::StrCat({SchemeOf(proxy.type), "://", ChromeHost(proxy), ":",
                       base::NumberToString(proxy.port)});
}

bool IsChallenger(const ProxyConfig& proxy, std::string_view challenger) {
  // Read as an http URL whatever the scheme, which keeps only its default
  // port: Chrome leaves out the scheme's own.
  const size_t separator = challenger.find("://");
  const std::string_view scheme = separator == std::string_view::npos
                                      ? "http"
                                      : challenger.substr(0, separator);
  const std::string_view rest = separator == std::string_view::npos
                                    ? challenger
                                    : challenger.substr(separator + 3);
  const GURL named(base::StrCat({"http://", rest}));
  if (!named.is_valid() || !named.has_host()) {
    return false;
  }
  const int port =
      named.has_port() ? named.IntPort() : (scheme == "https" ? 443 : 80);
  return port == proxy.port &&
         CanonicalHost(named.host()) == CanonicalHost(ConnectionHost(proxy));
}

std::string ConnectionHost(const ProxyConfig& proxy) {
  return std::string(WithoutBrackets(proxy.host));
}

std::string_view WebRTCPolicyForProfile(bool proxied) {
  return proxied ? "disable_non_proxied_udp" : "default_public_interface_only";
}

}  // namespace pane::model
