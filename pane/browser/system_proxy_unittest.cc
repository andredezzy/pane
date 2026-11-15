// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/system_proxy.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/observer_list.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "components/prefs/testing_pref_service.h"
#include "components/proxy_config/pref_proxy_config_tracker_impl.h"
#include "components/proxy_config/proxy_config_dictionary.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "components/proxy_config/proxy_prefs.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/proxy_config_with_annotation.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace pane {
namespace {

// The system's settings as Chromium reads them (net::ProxyConfigService's
// system readings, ProxyConfigServiceWin::SetFromIEConfig on Windows):
// "Automatically detect settings", "Use setup script", and "Use a proxy
// server" with its exceptions, each read into the config Chromium makes.
net::ProxyConfig System(bool detect,
                        std::optional<std::string> pac = std::nullopt,
                        std::optional<std::string> servers = std::nullopt,
                        std::optional<std::string> bypass = std::nullopt) {
  net::ProxyConfig config;
  config.set_auto_detect(detect);
  if (pac) {
    config.set_pac_url(GURL(*pac));
  }
  if (servers) {
    config.proxy_rules().ParseFromString(*servers);
  }
  if (bypass) {
    config.proxy_rules().bypass_rules.ParseFromString(*bypass);
  }
  config.set_from_system(true);
  return config;
}

net::ProxyConfigWithAnnotation Annotated(const net::ProxyConfig& config) {
  return net::ProxyConfigWithAnnotation(config, TRAFFIC_ANNOTATION_FOR_TESTS);
}

// What Chrome's own prefs make of the pref's value: the config a network
// context takes from it.
net::ProxyConfig FromPref(const base::DictValue& pref) {
  net::ProxyConfigWithAnnotation config;
  EXPECT_TRUE(PrefProxyConfigTrackerImpl::PrefConfigToNetConfig(
      ProxyConfigDictionary(pref.Clone()), &config));
  return config.value();
}

ProxyPrefs::ProxyMode ModeOf(const base::DictValue& pref) {
  ProxyPrefs::ProxyMode mode = ProxyPrefs::kModeCount;
  EXPECT_TRUE(ProxyConfigDictionary(pref.Clone()).GetMode(&mode));
  return mode;
}

TEST(WithoutAutoDetectionTest, AnAddressOfASetupScriptStays) {
  const net::ProxyConfig config =
      WithoutAutoDetection(
          Annotated(System(/*detect=*/true, "http://corp.example/proxy.pac")))
          .value();
  EXPECT_FALSE(config.auto_detect());
  EXPECT_EQ(config.pac_url(), GURL("http://corp.example/proxy.pac"));
  EXPECT_TRUE(config.from_system());
}

TEST(WithoutAutoDetectionTest, AServerAndItsExceptionsStay) {
  const net::ProxyConfig system =
      System(/*detect=*/true, std::nullopt, "proxy.example:8080",
             "<local>;*.corp.example;10.0.0.0/8");
  const net::ProxyConfig config =
      WithoutAutoDetection(Annotated(system)).value();
  EXPECT_FALSE(config.HasAutomaticSettings());
  EXPECT_TRUE(config.proxy_rules().Equals(system.proxy_rules()));
  EXPECT_EQ(config.proxy_rules().bypass_rules.ToString(),
            "<local>;*.corp.example;10.0.0.0/8;");
}

TEST(WithoutAutoDetectionTest, AServerForEachSchemeStays) {
  const net::ProxyConfig system =
      System(/*detect=*/true, std::nullopt,
             "http=web.example:80;https=tls.example:443;ftp=files.example:21;"
             "socks=socks.example:1080");
  const net::ProxyConfig config =
      WithoutAutoDetection(Annotated(system)).value();
  EXPECT_FALSE(config.auto_detect());
  EXPECT_TRUE(config.proxy_rules().Equals(system.proxy_rules()));
}

TEST(WithoutAutoDetectionTest, DetectionAloneIsDirect) {
  const net::ProxyConfig config =
      WithoutAutoDetection(Annotated(System(/*detect=*/true))).value();
  EXPECT_FALSE(config.HasAutomaticSettings());
  EXPECT_TRUE(config.proxy_rules().empty());
}

TEST(WithoutAutoDetectionTest, SettingsWithoutDetectionAreUnchanged) {
  const net::ProxyConfig system = System(
      /*detect=*/false, "http://corp.example/proxy.pac", "proxy.example:8080");
  EXPECT_TRUE(WithoutAutoDetection(Annotated(system)).value().Equals(system));
}

// A profile's own proxy, and detection a policy or the command line asks for
// (--proxy-auto-detect, ProxyMode "auto_detect"): not the system's.
TEST(WithoutAutoDetectionTest, SettingsNotTheSystemsAreUntouched) {
  net::ProxyConfig own;
  own.proxy_rules().ParseFromString("socks5://127.0.0.1:1080");
  EXPECT_TRUE(WithoutAutoDetection(Annotated(own)).value().Equals(own));

  net::ProxyConfig asked = net::ProxyConfig::CreateAutoDetect();
  ASSERT_FALSE(asked.from_system());
  EXPECT_TRUE(WithoutAutoDetection(Annotated(asked)).value().auto_detect());
}

TEST(ProxyPrefWithoutAutoDetectionTest, ASetupScriptIsItsAddress) {
  const base::DictValue pref = ProxyPrefWithoutAutoDetection(
      System(/*detect=*/false, "http://corp.example/proxy.pac"));
  EXPECT_EQ(ModeOf(pref), ProxyPrefs::MODE_PAC_SCRIPT);
  const net::ProxyConfig config = FromPref(pref);
  EXPECT_EQ(config.pac_url(), GURL("http://corp.example/proxy.pac"));
  EXPECT_FALSE(config.pac_mandatory());
  EXPECT_FALSE(config.auto_detect());
}

TEST(ProxyPrefWithoutAutoDetectionTest, DetectionWithAScriptIsTheScript) {
  const base::DictValue pref = ProxyPrefWithoutAutoDetection(
      System(/*detect=*/true, "http://corp.example/proxy.pac"));
  EXPECT_EQ(ModeOf(pref), ProxyPrefs::MODE_PAC_SCRIPT);
  EXPECT_FALSE(FromPref(pref).auto_detect());
}

TEST(ProxyPrefWithoutAutoDetectionTest, DetectionAloneIsDirect) {
  EXPECT_EQ(ModeOf(ProxyPrefWithoutAutoDetection(System(/*detect=*/true))),
            ProxyPrefs::MODE_DIRECT);
  EXPECT_EQ(ModeOf(ProxyPrefWithoutAutoDetection(System(/*detect=*/false))),
            ProxyPrefs::MODE_DIRECT);
}

// Each form reads back as the very rules the system's settings gave.
TEST(ProxyPrefWithoutAutoDetectionTest, ServersAndExceptionsReadBackTheSame) {
  for (const auto& [servers, bypass] : {
           // Windows' one server for every scheme, its local addresses and
           // exceptions.
           std::pair("proxy.example:8080", "<local>;*.corp.example"),
           // A server for each scheme, and SOCKS for the rest, as Windows'
           // per-scheme form reads ("socks=" is SOCKS4 there).
           std::pair("http=web.example:80;https=tls.example:443;"
                     "ftp=files.example:21;socks=socks.example:1080",
                     "intranet;192.168.*"),
           // The Mac's SOCKS, SOCKS5, with its simple hostnames.
           std::pair("http=web.example:80;socks=socks5://socks.example:1080",
                     "<local>;*.local;169.254/16"),
           // HTTPS and HTTP proxies, an IPv6 literal, a fallback, and direct.
           std::pair("https://tls.example:443,[::1]:3128,direct://",
                     "<-loopback>"),
           // An HTTP proxy in the list for what the others don't name.
           std::pair("https=tls.example:443;socks=http://web.example:80", ""),
       }) {
    SCOPED_TRACE(servers);
    const net::ProxyConfig system =
        System(/*detect=*/true, std::nullopt, servers, bypass);
    const base::DictValue pref = ProxyPrefWithoutAutoDetection(system);
    EXPECT_EQ(ModeOf(pref), ProxyPrefs::MODE_FIXED_SERVERS);
    const net::ProxyConfig config = FromPref(pref);
    EXPECT_FALSE(config.HasAutomaticSettings());
    EXPECT_TRUE(config.proxy_rules().Equals(system.proxy_rules()));
    EXPECT_EQ(config.proxy_rules().bypass_rules.ToString(),
              system.proxy_rules().bypass_rules.ToString());
  }
}

// Chromium's system reading, as a test sets it.
class FakeSystem : public net::ProxyConfigService {
 public:
  void Report(const net::ProxyConfig& config) {
    config_ = Annotated(config);
    for (Observer& observer : observers_) {
      observer.OnProxyConfigChanged(*config_, CONFIG_VALID);
    }
  }

  // net::ProxyConfigService:
  void AddObserver(Observer* observer) override {
    observers_.AddObserver(observer);
  }
  void RemoveObserver(Observer* observer) override {
    observers_.RemoveObserver(observer);
  }
  ConfigAvailability GetLatestProxyConfig(
      net::ProxyConfigWithAnnotation* config) override {
    if (!config_) {
      return CONFIG_PENDING;
    }
    *config = *config_;
    return CONFIG_VALID;
  }

 private:
  std::optional<net::ProxyConfigWithAnnotation> config_;
  base::ObserverList<Observer>::Unchecked observers_;
};

class SystemProxyTest : public testing::Test {
 protected:
  SystemProxyTest() {
    PrefProxyConfigTrackerImpl::RegisterPrefs(local_state_.registry());
  }

  // What the browser's own network contexts take from local state, as
  // Chrome's reading of its prefs makes it, and whether it outranks the
  // system's settings.
  net::ProxyConfig Effective(ProxyPrefs::ConfigState* state) {
    net::ProxyConfigWithAnnotation config;
    *state = PrefProxyConfigTrackerImpl::ReadPrefConfig(&local_state_, &config,
                                                        nullptr);
    return config.value();
  }

  TestingPrefServiceSimple local_state_;
};

TEST_F(SystemProxyTest, LocalStateHoldsDirectUntilTheFirstReading) {
  SystemProxy::HoldLocalState(local_state_);
  ProxyPrefs::ConfigState state;
  const net::ProxyConfig config = Effective(&state);
  EXPECT_TRUE(PrefProxyConfigTrackerImpl::PrefPrecedes(state));
  EXPECT_FALSE(config.HasAutomaticSettings());
  EXPECT_TRUE(config.proxy_rules().empty());
}

TEST_F(SystemProxyTest, LocalStateKeepsTheLastLaunchsReading) {
  local_state_.SetDict(
      proxy_config::prefs::kProxy,
      ProxyConfigDictionary::CreateFixedServers("proxy.example:8080", ""));
  SystemProxy::HoldLocalState(local_state_);
  ProxyPrefs::ConfigState state;
  EXPECT_EQ(Effective(&state).proxy_rules().single_proxies.ToDebugString(),
            "PROXY proxy.example:8080");
}

TEST_F(SystemProxyTest, LocalStateFollowsTheSystemWithoutDetection) {
  auto owned = std::make_unique<FakeSystem>();
  FakeSystem& system = *owned;
  SystemProxy::HoldLocalState(local_state_);
  SystemProxy proxy(local_state_, std::move(owned));
  EXPECT_EQ(SystemProxy::Get(), &proxy);

  bool read = false;
  proxy.WhenRead(base::BindOnce([](bool* read) { *read = true; }, &read));
  EXPECT_FALSE(read);

  // Windows as it ships: detection alone.
  system.Report(System(/*detect=*/true));
  EXPECT_TRUE(read);
  ProxyPrefs::ConfigState state;
  net::ProxyConfig config = Effective(&state);
  EXPECT_TRUE(PrefProxyConfigTrackerImpl::PrefPrecedes(state));
  EXPECT_FALSE(config.HasAutomaticSettings());
  EXPECT_TRUE(config.proxy_rules().empty());

  // Then a setup script, then a server, as Windows' settings change.
  system.Report(System(/*detect=*/true, "http://corp.example/proxy.pac"));
  config = Effective(&state);
  EXPECT_FALSE(config.auto_detect());
  EXPECT_EQ(config.pac_url(), GURL("http://corp.example/proxy.pac"));

  system.Report(
      System(/*detect=*/false, std::nullopt, "proxy.example:8080", "<local>"));
  config = Effective(&state);
  EXPECT_FALSE(config.HasAutomaticSettings());
  EXPECT_EQ(config.proxy_rules().single_proxies.ToDebugString(),
            "PROXY proxy.example:8080");
  EXPECT_EQ(config.proxy_rules().bypass_rules.ToString(), "<local>;");

  bool again = false;
  proxy.WhenRead(base::BindOnce([](bool* again) { *again = true; }, &again));
  EXPECT_TRUE(again);
}

TEST_F(SystemProxyTest, APolicysProxyKeepsItsPrecedence) {
  auto owned = std::make_unique<FakeSystem>();
  FakeSystem& system = *owned;
  SystemProxy proxy(local_state_, std::move(owned));
  local_state_.SetManagedPref(
      proxy_config::prefs::kProxy,
      ProxyConfigDictionary::CreateFixedServers("policy.example:3128", ""));
  system.Report(System(/*detect=*/false, std::nullopt, "proxy.example:8080"));

  ProxyPrefs::ConfigState state;
  const net::ProxyConfig config = Effective(&state);
  EXPECT_EQ(state, ProxyPrefs::CONFIG_POLICY);
  EXPECT_EQ(config.proxy_rules().single_proxies.ToDebugString(),
            "PROXY policy.example:3128");
}

// A network context's end of the settings Chrome sends it.
class Context : public network::mojom::ProxyConfigClient {
 public:
  explicit Context(
      mojo::PendingReceiver<network::mojom::ProxyConfigClient> receiver)
      : receiver_(this, std::move(receiver)) {}

  net::ProxyConfig Next() { return future_.Take().value(); }
  void WhenEnded(base::OnceClosure ended) {
    receiver_.set_disconnect_handler(std::move(ended));
  }

  // network::mojom::ProxyConfigClient:
  void OnProxyConfigUpdated(
      const net::ProxyConfigWithAnnotation& proxy_config) override {
    future_.SetValue(proxy_config);
  }
  void FlushProxyConfig(FlushProxyConfigCallback callback) override {
    std::move(callback).Run();
  }

 private:
  mojo::Receiver<network::mojom::ProxyConfigClient> receiver_;
  base::test::TestFuture<net::ProxyConfigWithAnnotation> future_;
};

class DropAutoDetectionTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    static bool initialized = false;
    if (!initialized) {
      mojo::core::Init();
      initialized = true;
    }
  }

  base::test::TaskEnvironment task_environment_;
};

// What a profile's network context starts with, and every change Chrome
// sends it after (ProxyConfigMonitor's), reach it without the system's
// detection; a profile's own proxy reaches it as it is.
TEST_F(DropAutoDetectionTest, TheSystemsDetectionNeverReachesAContext) {
  network::mojom::NetworkContextParams params;
  params.initial_proxy_config = Annotated(System(/*detect=*/true));
  mojo::Remote<network::mojom::ProxyConfigClient> chrome;
  params.proxy_config_client_receiver = chrome.BindNewPipeAndPassReceiver();

  DropAutoDetection(params);

  ASSERT_TRUE(params.initial_proxy_config);
  EXPECT_FALSE(params.initial_proxy_config->value().HasAutomaticSettings());
  Context context(std::move(params.proxy_config_client_receiver));

  chrome->OnProxyConfigUpdated(
      Annotated(System(/*detect=*/true, "http://corp.example/proxy.pac")));
  net::ProxyConfig config = context.Next();
  EXPECT_FALSE(config.auto_detect());
  EXPECT_EQ(config.pac_url(), GURL("http://corp.example/proxy.pac"));

  chrome->OnProxyConfigUpdated(Annotated(System(
      /*detect=*/true, std::nullopt, "proxy.example:8080", "<local>")));
  config = context.Next();
  EXPECT_FALSE(config.HasAutomaticSettings());
  EXPECT_EQ(config.proxy_rules().bypass_rules.ToString(), "<local>;");

  net::ProxyConfig own;
  own.proxy_rules().ParseFromString("socks5://127.0.0.1:1080");
  chrome->OnProxyConfigUpdated(Annotated(own));
  EXPECT_TRUE(context.Next().Equals(own));

  base::test::TestFuture<void> flushed;
  chrome->FlushProxyConfig(flushed.GetCallback());
  EXPECT_TRUE(flushed.Wait());
}

// A context that takes no settings from Chrome (a proxy test's, with its
// one rule) keeps its own.
TEST_F(DropAutoDetectionTest, AContextWithItsOwnRuleKeepsIt) {
  network::mojom::NetworkContextParams params;
  net::ProxyConfig own;
  own.proxy_rules().ParseFromString("http://proxy.example:8080");
  params.initial_proxy_config = Annotated(own);

  DropAutoDetection(params);

  EXPECT_TRUE(params.initial_proxy_config->value().Equals(own));
  EXPECT_FALSE(params.proxy_config_client_receiver.is_valid());
}

// Chrome's end going (its profile's) takes the filter with it, and the
// context sees its settings end, as without the filter.
TEST_F(DropAutoDetectionTest, ChromesEndGoingEndsTheContexts) {
  network::mojom::NetworkContextParams params;
  mojo::Remote<network::mojom::ProxyConfigClient> chrome;
  params.proxy_config_client_receiver = chrome.BindNewPipeAndPassReceiver();
  DropAutoDetection(params);

  Context context(std::move(params.proxy_config_client_receiver));
  base::RunLoop ended;
  context.WhenEnded(ended.QuitClosure());
  chrome.reset();
  ended.Run();
}

}  // namespace
}  // namespace pane
