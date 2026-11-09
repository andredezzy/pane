// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/proxy_route.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's ProxyRouteTests.

ProxyConfig Proxy(ProxyType type,
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

// An IPv6 host takes brackets for Chrome, typed with them or not, and none
// for a connection.
TEST(ProxyRouteTest, Ipv6Host) {
  const ProxyConfig bare =
      Proxy(ProxyType::kSocks5, "2606:4700:4700::1111", 1080);
  const ProxyConfig bracketed =
      Proxy(ProxyType::kHttp, "[2606:4700:4700::1111]", 3128);
  const ProxyConfig named = Proxy(ProxyType::kHttp, "proxy.example", 3128);

  EXPECT_EQ(ProxyRules(bare, std::nullopt),
            "socks5://[2606:4700:4700::1111]:1080");
  EXPECT_EQ(ProxyRules(bracketed, std::nullopt),
            "http://[2606:4700:4700::1111]:3128");
  EXPECT_TRUE(IsChallenger(bare, "socks5://[2606:4700:4700::1111]:1080"));
  EXPECT_TRUE(IsChallenger(bracketed, "http://[2606:4700:4700::1111]:3128"));
  EXPECT_EQ(ConnectionHost(bare), "2606:4700:4700::1111");
  EXPECT_EQ(ConnectionHost(bracketed), "2606:4700:4700::1111");
  EXPECT_EQ(ConnectionHost(named), "proxy.example");
}

// HTTP and HTTPS proxies go to Chrome as they are, credentials and all.
TEST(ProxyRouteTest, HttpProxies) {
  const ProxyConfig http =
      Proxy(ProxyType::kHttp, "proxy.example", 8080, "u", "p");
  const ProxyConfig https = Proxy(ProxyType::kHttps, "proxy.example", 443);

  EXPECT_FALSE(NeedsRelay(http));
  EXPECT_EQ(ProxyRules(http, std::nullopt), "http://proxy.example:8080");
  EXPECT_EQ(ProxyRules(https, std::nullopt), "https://proxy.example:443");
}

// SOCKS without credentials goes straight to Chrome.
TEST(ProxyRouteTest, SocksWithoutCredentials) {
  const ProxyConfig socks5 = Proxy(ProxyType::kSocks5, "10.0.0.2", 1080);
  const ProxyConfig socks4 = Proxy(ProxyType::kSocks4, "10.0.0.2", 1080, "");

  EXPECT_FALSE(NeedsRelay(socks5));
  EXPECT_FALSE(NeedsRelay(socks4));
  EXPECT_EQ(ProxyRules(socks5, 5555), "socks5://10.0.0.2:1080");
  EXPECT_EQ(ProxyRules(socks4, std::nullopt), "socks4://10.0.0.2:1080");
}

// SOCKS with credentials goes through the local relay.
TEST(ProxyRouteTest, SocksWithCredentials) {
  const ProxyConfig socks5 =
      Proxy(ProxyType::kSocks5, "10.0.0.2", 1080, "u", "p");
  const ProxyConfig socks4 = Proxy(ProxyType::kSocks4, "10.0.0.2", 1080, "u");

  EXPECT_TRUE(NeedsRelay(socks5));
  EXPECT_TRUE(NeedsRelay(socks4));
  EXPECT_EQ(ProxyRules(socks5, 54321), "socks5://127.0.0.1:54321");
  EXPECT_EQ(ProxyRules(socks4, 54321), "socks5://127.0.0.1:54321");
}

// A challenge is the proxy's when it names its host and port.
TEST(ProxyRouteTest, Challenger) {
  const ProxyConfig proxy = Proxy(ProxyType::kHttp, "Proxy.example", 3128);

  EXPECT_TRUE(IsChallenger(proxy, "http://proxy.example:3128"));
  EXPECT_TRUE(IsChallenger(proxy, "proxy.example:3128"));
  EXPECT_FALSE(IsChallenger(proxy, "http://proxy.example:8080"));
  EXPECT_FALSE(IsChallenger(proxy, "https://other.example:3128"));
}

// A challenge is matched in the form Chrome names it, whatever was typed.
TEST(ProxyRouteTest, ChallengerForms) {
  const ProxyConfig ipv6 = Proxy(ProxyType::kHttp, "2001:0DB8:0:0::0001", 3128);
  EXPECT_TRUE(IsChallenger(ipv6, "http://[2001:db8::1]:3128"));
  EXPECT_FALSE(IsChallenger(ipv6, "http://[2001:db8::2]:3128"));
  EXPECT_FALSE(IsChallenger(ipv6, "http://[2001:db8::1]:8080"));

  const ProxyConfig bracketed =
      Proxy(ProxyType::kHttp, "[0:0:0:0:0:0:0:1]", 3128);
  EXPECT_TRUE(IsChallenger(bracketed, "http://[::1]:3128"));

  // Chrome leaves out the scheme's own port.
  EXPECT_TRUE(IsChallenger(Proxy(ProxyType::kHttp, "proxy.example", 80),
                           "http://proxy.example"));
  EXPECT_TRUE(IsChallenger(Proxy(ProxyType::kHttps, "proxy.example", 443),
                           "https://proxy.example"));
  EXPECT_FALSE(IsChallenger(Proxy(ProxyType::kHttp, "proxy.example", 443),
                            "http://proxy.example"));
  // A name in punycode.
  EXPECT_TRUE(IsChallenger(Proxy(ProxyType::kHttp, "exämple.test", 3128),
                           "http://xn--exmple-cua.test:3128"));
}

// A proxied profile keeps all UDP to the proxy; others hide private
// interfaces.
TEST(ProxyRouteTest, WebRTC) {
  EXPECT_EQ(WebRTCPolicyForProfile(true), "disable_non_proxied_udp");
  EXPECT_EQ(WebRTCPolicyForProfile(false), "default_public_interface_only");
}

}  // namespace
}  // namespace pane::model
