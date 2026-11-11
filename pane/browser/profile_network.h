// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_PROFILE_NETWORK_H_
#define PANE_BROWSER_PROFILE_NETWORK_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "pane/model/profile.h"

namespace net {
class AuthChallengeInfo;
class AuthCredentials;
}  // namespace net

namespace pane {

namespace model {
class BrowserModel;
}

class BrowserContext;
class ProxyTest;
class SocksRelay;

// Each profile's network (ADR 0037), as the Electron app set up a session
// (apps/veil profile/profile.ts) and the Swift app's ProfileNetwork did: its
// proxy as Chrome's own fixed rule, in the profile's prefs, through a local
// relay for SOCKS with credentials; the WebRTC policy that keeps the real
// address from leaking past it; the proxy's sign-in, answered with the
// profile's credentials; and the proxy's connection test, for the sidebar's
// dot. A port outside 1 to 65535 goes through the relay too, which refuses
// it, so the profile's pages fail rather than go direct, as Chrome would take
// a rule it can't read. On the UI thread.
class ProfileNetwork {
 public:
  explicit ProfileNetwork(model::BrowserModel& model);
  ProfileNetwork(const ProfileNetwork&) = delete;
  ProfileNetwork& operator=(const ProfileNetwork&) = delete;
  ~ProfileNetwork();

  // The app's, while it runs: the window's sign-in asks it for a proxy's
  // credentials.
  static ProfileNetwork* Get();

  // Applies the profile's proxy to its open context, before any of its
  // pages loads, then runs `applied`. A proxy changed while its relay
  // started (the sheet saved as the profile opened) is applied in its
  // place, never dropped: two applies at once both end on the profile's
  // current proxy.
  void Apply(BrowserContext& context, base::OnceClosure applied);
  // The profile's context closed: its relay goes, and what was answered of
  // its proxy is forgotten.
  void Stop(const std::string& profile_id);
  // The profile went: its test too.
  void Forget(const std::string& profile_id);

  // The profile's credentials for a challenge from its own proxy, once:
  // asked again within 5 s, they were wrong, and the user is asked instead.
  std::optional<net::AuthCredentials> CredentialsFor(
      const std::string& profile_id,
      const net::AuthChallengeInfo& challenge);

  // Tests the profile's proxy and records the outcome for the sidebar: none
  // without a proxy.
  void Test(const std::string& profile_id);

 private:
  struct Relay {
    Relay();
    Relay(Relay&&);
    Relay& operator=(Relay&&);
    ~Relay();

    std::unique_ptr<SocksRelay> relay;
    // Once it has started: its port, or none when it could not listen.
    bool started = false;
    std::optional<int> port;
    // Applies that wait for it.
    std::vector<base::OnceClosure> waiting;
  };

  void Applying(base::WeakPtr<BrowserContext> context,
                base::OnceClosure applied);
  void RelayStarted(const std::string& profile_id,
                    const model::ProxyConfig& proxy,
                    std::optional<int> port);
  void Tested(const std::string& profile_id,
              const model::ProxyConfig& proxy,
              bool connected,
              const std::string& error);

  const raw_ref<model::BrowserModel> model_;
  std::map<std::string, Relay> relays_;
  std::map<std::string, std::unique_ptr<ProxyTest>> tests_;
  // When each profile's proxy was last answered, to tell wrong credentials
  // (asked again at once) from a new challenge.
  std::map<std::string, base::TimeTicks> last_answer_;
  base::WeakPtrFactory<ProfileNetwork> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_BROWSER_PROFILE_NETWORK_H_
