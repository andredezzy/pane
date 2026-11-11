// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/profile_network.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "net/base/auth.h"
#include "pane/browser/browser_context.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/proxy_test.h"
#include "pane/browser/socks_relay.h"
#include "pane/model/browser_model.h"
#include "pane/model/profile_form.h"
#include "pane/model/proxy_route.h"
#include "url/scheme_host_port.h"

namespace pane {

namespace {

ProfileNetwork* g_network = nullptr;

// Asked again this soon, the credentials were wrong.
constexpr base::TimeDelta kAnsweredAgain = base::Seconds(5);

// A SOCKS proxy with a username goes through the relay, which signs in for
// Chrome; so does a port Chrome can't read, which the relay refuses.
bool ThroughRelay(const model::ProxyConfig& proxy) {
  return model::NeedsRelay(proxy) || !model::IsProxyPort(proxy.port);
}

}  // namespace

ProfileNetwork::Relay::Relay() = default;
ProfileNetwork::Relay::Relay(Relay&&) = default;
ProfileNetwork::Relay& ProfileNetwork::Relay::operator=(Relay&&) = default;
ProfileNetwork::Relay::~Relay() = default;

ProfileNetwork::ProfileNetwork(model::BrowserModel& model) : model_(model) {
  CHECK(!g_network);
  g_network = this;
}

ProfileNetwork::~ProfileNetwork() {
  g_network = nullptr;
}

// static
ProfileNetwork* ProfileNetwork::Get() {
  return g_network;
}

void ProfileNetwork::Apply(BrowserContext& context, base::OnceClosure applied) {
  Applying(context.GetWeakPtr(), std::move(applied));
}

// Each time with the profile's proxy as it is then: after a relay's start,
// the proxy changed meanwhile is the one applied.
void ProfileNetwork::Applying(base::WeakPtr<BrowserContext> context,
                              base::OnceClosure applied) {
  if (!context) {
    std::move(applied).Run();
    return;
  }
  const std::string id = context->id();
  const model::BrowserProfile* profile = model_->FindProfile(id);
  const std::optional<model::ProxyConfig> proxy =
      profile ? profile->proxy : std::nullopt;

  std::optional<std::string> rules;
  if (proxy && ThroughRelay(*proxy)) {
    Relay& relay = relays_[id];
    if (!relay.relay || relay.relay->upstream() != *proxy) {
      // Another proxy's relay goes, and what waited for it waits for this
      // one, as its proxy is the profile's now.
      std::vector<base::OnceClosure> waiting = std::move(relay.waiting);
      relay = Relay();
      relay.waiting = std::move(waiting);
      relay.relay = std::make_unique<SocksRelay>(
          *proxy, base::BindOnce(&ProfileNetwork::RelayStarted,
                                 weak_factory_.GetWeakPtr(), id, *proxy));
    }
    if (!relay.started) {
      relay.waiting.push_back(base::BindOnce(&ProfileNetwork::Applying,
                                             weak_factory_.GetWeakPtr(),
                                             context, std::move(applied)));
      return;
    }
    if (!relay.port) {
      LOG(ERROR) << "A profile's SOCKS relay could not listen: its pages "
                    "fail at its proxy";
    }
    rules = model::ProxyRules(*proxy, relay.port);
  } else {
    if (proxy) {
      rules = model::ProxyRules(*proxy, std::nullopt);
    }
    // A relay that was starting for the proxy before goes, and what waited
    // for it applies this one.
    if (const auto found = relays_.find(id); found != relays_.end()) {
      std::vector<base::OnceClosure> waiting =
          std::exchange(found->second.waiting, {});
      relays_.erase(found);
      for (base::OnceClosure& apply : waiting) {
        std::move(apply).Run();
      }
      if (!context) {
        std::move(applied).Run();
        return;
      }
    }
  }

  context->SetProxyRules(rules);
  context->SetWebRTCIPHandlingPolicy(
      model::WebRTCPolicyForProfile(proxy.has_value()));
  std::move(applied).Run();
}

void ProfileNetwork::RelayStarted(const std::string& profile_id,
                                  const model::ProxyConfig& proxy,
                                  std::optional<int> port) {
  const auto found = relays_.find(profile_id);
  // Another proxy's took its place meanwhile.
  if (found == relays_.end() || !found->second.relay ||
      found->second.relay->upstream() != proxy) {
    return;
  }
  found->second.started = true;
  found->second.port = port;
  std::vector<base::OnceClosure> waiting =
      std::exchange(found->second.waiting, {});
  for (base::OnceClosure& apply : waiting) {
    std::move(apply).Run();
  }
}

void ProfileNetwork::Stop(const std::string& profile_id) {
  const auto found = relays_.find(profile_id);
  if (found != relays_.end()) {
    // What waits for it is answered with no relay: the profile is closing.
    std::vector<base::OnceClosure> waiting =
        std::exchange(found->second.waiting, {});
    relays_.erase(found);
    for (base::OnceClosure& apply : waiting) {
      std::move(apply).Run();
    }
  }
  last_answer_.erase(profile_id);
}

void ProfileNetwork::Forget(const std::string& profile_id) {
  Stop(profile_id);
  tests_.erase(profile_id);
}

std::optional<net::AuthCredentials> ProfileNetwork::CredentialsFor(
    const std::string& profile_id,
    const net::AuthChallengeInfo& challenge) {
  const model::BrowserProfile* profile = model_->FindProfile(profile_id);
  if (!challenge.is_proxy || !profile || !profile->proxy ||
      profile->proxy->username.value_or("").empty() ||
      !model::IsChallenger(*profile->proxy, challenge.challenger.Serialize())) {
    return std::nullopt;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  if (const auto last = last_answer_.find(profile_id);
      last != last_answer_.end() && now - last->second < kAnsweredAgain) {
    last_answer_.erase(last);
    return std::nullopt;
  }
  last_answer_[profile_id] = now;
  return net::AuthCredentials(
      base::UTF8ToUTF16(*profile->proxy->username),
      base::UTF8ToUTF16(profile->proxy->password.value_or("")));
}

void ProfileNetwork::Test(const std::string& profile_id) {
  const model::BrowserProfile* profile = model_->FindProfile(profile_id);
  if (!profile || !profile->proxy) {
    tests_.erase(profile_id);
    model_->SetProxyStatus(profile_id, std::nullopt);
    return;
  }
  model_->SetProxyStatus(profile_id,
                         model::BrowserModel::ProxyStatus::kTesting);
  const model::ProxyConfig proxy = *profile->proxy;
  // A test of another proxy, still running, goes: its answer is no longer
  // the profile's.
  tests_[profile_id] = std::make_unique<ProxyTest>(
      proxy,
      base::BindOnce(
          [](base::WeakPtr<ProfileNetwork> network, std::string id,
             model::ProxyConfig proxy, const ProxyTest::Outcome& outcome) {
            if (network) {
              network->Tested(id, proxy, outcome.ip.has_value(), outcome.error);
            }
          },
          weak_factory_.GetWeakPtr(), profile_id, proxy));
}

void ProfileNetwork::Tested(const std::string& profile_id,
                            const model::ProxyConfig& proxy,
                            bool connected,
                            const std::string& error) {
  PANE_TRACE("callback", "ProxyTest.done",
             base::DictValue()
                 .Set("identifier", profile_id)
                 .Set("connected", connected)
                 .Set("error", error));
  // The test goes once its answer is in; this runs inside its last call,
  // which touches nothing of it after.
  tests_.erase(profile_id);
  const model::BrowserProfile* profile = model_->FindProfile(profile_id);
  // A newer proxy is being tested by then.
  if (!profile || profile->proxy != proxy) {
    return;
  }
  if (!connected) {
    LOG(WARNING) << "A profile's proxy failed its test: " << error;
  }
  model_->SetProxyStatus(
      profile_id, connected ? model::BrowserModel::ProxyStatus::kConnected
                            : model::BrowserModel::ProxyStatus::kFailed);
}

}  // namespace pane
