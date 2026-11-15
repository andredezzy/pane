// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/system_proxy.h"

#include <string>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "components/prefs/pref_service.h"
#include "components/proxy_config/proxy_config_dictionary.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/proxy_chain.h"
#include "net/base/proxy_server.h"
#include "net/base/proxy_string_util.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "net/proxy_resolution/proxy_list.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/proxy_config_with_annotation.mojom.h"

namespace pane {

namespace {

SystemProxy* g_system_proxy = nullptr;

// A proxy list as the pref's servers take it (the command line's
// --proxy-server's form): each proxy's URI, in order, with its scheme, which
// Chromium leaves off for HTTP, so a "socks=" list, which reads a bare URI as
// SOCKS4, reads it as HTTP still.
std::string Servers(const net::ProxyList& list) {
  std::vector<std::string> uris;
  for (const net::ProxyChain& chain : list.AllChains()) {
    if (chain.is_direct()) {
      uris.push_back("direct://");
      continue;
    }
    // A chain of several proxies, which no system's settings name, has no
    // form there.
    if (!chain.is_single_proxy()) {
      continue;
    }
    const net::ProxyServer& server = chain.First();
    const std::string uri = net::ProxyServerToProxyUri(server);
    uris.push_back(server.is_http() ? base::StrCat({"http://", uri}) : uri);
  }
  return base::JoinString(uris, ",");
}

// The servers of `rules` as the pref takes them: one list for every scheme,
// or a list for each ("http=…;https=…;ftp=…;socks=…", socks being the list
// for whatever the others don't name).
std::string Servers(const net::ProxyConfig::ProxyRules& rules) {
  if (rules.type == net::ProxyConfig::ProxyRules::Type::PROXY_LIST) {
    return Servers(rules.single_proxies);
  }
  std::vector<std::string> per_scheme;
  for (const auto& [scheme, list] :
       {std::pair("http", &rules.proxies_for_http),
        std::pair("https", &rules.proxies_for_https),
        std::pair("ftp", &rules.proxies_for_ftp),
        std::pair("socks", &rules.fallback_proxies)}) {
    if (!list->IsEmpty()) {
      per_scheme.push_back(base::StrCat({scheme, "=", Servers(*list)}));
    }
  }
  return base::JoinString(per_scheme, ";");
}

// Between Chrome's proxy settings for a network context and the context:
// what Chrome sends, the system's without their detection. Goes with either
// end: the context's (a network service that restarts gets a new one), or
// Chrome's (the profile's going).
class WithoutDetection : public network::mojom::ProxyConfigClient {
 public:
  WithoutDetection(
      mojo::PendingReceiver<network::mojom::ProxyConfigClient> from_chrome,
      mojo::PendingRemote<network::mojom::ProxyConfigClient> to_context)
      : receiver_(this, std::move(from_chrome)),
        context_(std::move(to_context)) {
    receiver_.set_disconnect_handler(
        base::BindOnce(&WithoutDetection::Gone, base::Unretained(this)));
    context_.set_disconnect_handler(
        base::BindOnce(&WithoutDetection::Gone, base::Unretained(this)));
  }
  WithoutDetection(const WithoutDetection&) = delete;
  WithoutDetection& operator=(const WithoutDetection&) = delete;
  ~WithoutDetection() override = default;

  // network::mojom::ProxyConfigClient:
  void OnProxyConfigUpdated(
      const net::ProxyConfigWithAnnotation& proxy_config) override {
    context_->OnProxyConfigUpdated(WithoutAutoDetection(proxy_config));
  }
  void FlushProxyConfig(FlushProxyConfigCallback callback) override {
    context_->FlushProxyConfig(std::move(callback));
  }

 private:
  void Gone() { delete this; }

  mojo::Receiver<network::mojom::ProxyConfigClient> receiver_;
  mojo::Remote<network::mojom::ProxyConfigClient> context_;
};

}  // namespace

net::ProxyConfigWithAnnotation WithoutAutoDetection(
    const net::ProxyConfigWithAnnotation& config) {
  // Chromium's reading of the system's settings marks what it reads
  // (ProxyConfig::from_system, which only its system readings set and which
  // Chrome's prefs and the network service carry along): a profile's own
  // proxy, a policy's, the command line's (--proxy-auto-detect included), or
  // an extension's never has it.
  if (!config.value().from_system() || !config.value().auto_detect()) {
    return config;
  }
  net::ProxyConfig without = config.value();
  without.set_auto_detect(false);
  return net::ProxyConfigWithAnnotation(without, config.traffic_annotation());
}

base::DictValue ProxyPrefWithoutAutoDetection(const net::ProxyConfig& system) {
  if (system.has_pac_url()) {
    return ProxyConfigDictionary::CreatePacScript(system.pac_url().spec(),
                                                  system.pac_mandatory());
  }
  const net::ProxyConfig::ProxyRules& rules = system.proxy_rules();
  if (rules.empty()) {
    return ProxyConfigDictionary::CreateDirect();
  }
  return ProxyConfigDictionary::CreateFixedServers(
      Servers(rules), rules.bypass_rules.ToString());
}

void DropAutoDetection(network::mojom::NetworkContextParams& params) {
  if (params.initial_proxy_config) {
    params.initial_proxy_config =
        WithoutAutoDetection(*params.initial_proxy_config);
  }
  if (!params.proxy_config_client_receiver.is_valid()) {
    return;
  }
  mojo::PendingRemote<network::mojom::ProxyConfigClient> to_context;
  mojo::PendingReceiver<network::mojom::ProxyConfigClient> from_chrome =
      std::exchange(params.proxy_config_client_receiver,
                    to_context.InitWithNewPipeAndPassReceiver());
  // Owns itself, and goes with either end.
  new WithoutDetection(std::move(from_chrome), std::move(to_context));
}

// static
void SystemProxy::HoldLocalState(PrefService& local_state) {
  // A value of the pref's own outranks the system's settings in Chrome's
  // reading of it (PrefProxyConfigTrackerImpl::ReadPrefConfig), and only a
  // value Pane set: nothing else of Chrome's sets local state's.
  if (!local_state.FindPreference(proxy_config::prefs::kProxy)
           ->HasUserSetting()) {
    local_state.SetDict(proxy_config::prefs::kProxy,
                        ProxyConfigDictionary::CreateDirect());
  }
}

SystemProxy::SystemProxy(PrefService& local_state,
                         std::unique_ptr<net::ProxyConfigService> system)
    : local_state_(local_state), system_(std::move(system)) {
  CHECK(!g_system_proxy);
  g_system_proxy = this;
  system_->AddObserver(this);
  net::ProxyConfigWithAnnotation config;
  // Read on a thread of the pool on Windows: answered through the observer.
  const net::ProxyConfigService::ConfigAvailability availability =
      system_->GetLatestProxyConfig(&config);
  if (availability != net::ProxyConfigService::CONFIG_PENDING) {
    OnProxyConfigChanged(config, availability);
  }
}

SystemProxy::~SystemProxy() {
  system_->RemoveObserver(this);
  g_system_proxy = nullptr;
}

// static
SystemProxy* SystemProxy::Get() {
  return g_system_proxy;
}

void SystemProxy::WhenRead(base::OnceClosure read) {
  if (read_) {
    std::move(read).Run();
    return;
  }
  waiting_.push_back(std::move(read));
}

void SystemProxy::OnProxyConfigChanged(
    const net::ProxyConfigWithAnnotation& config,
    net::ProxyConfigService::ConfigAvailability availability) {
  Read(availability == net::ProxyConfigService::CONFIG_VALID
           ? config.value()
           : net::ProxyConfig::CreateDirect());
}

void SystemProxy::Read(const net::ProxyConfig& system) {
  // Unchanged, the pref's store writes nothing.
  local_state_->SetDict(proxy_config::prefs::kProxy,
                        ProxyPrefWithoutAutoDetection(system));
  read_ = true;
  for (base::OnceClosure& read : std::exchange(waiting_, {})) {
    std::move(read).Run();
  }
}

}  // namespace pane
