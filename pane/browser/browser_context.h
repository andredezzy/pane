// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_BROWSER_CONTEXT_H_
#define PANE_BROWSER_BROWSER_CONTEXT_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"

class Profile;
class ScopedProfileKeepAlive;

namespace pane {

// One of Pane's profiles while it is open in the engine: its Chrome profile
// (ADR 0006), held loaded while Pane shows it, as an open window holds one,
// and what Pane sets on it. The Swift app's BrowserContext, which the
// bridge's PaneBrowserContext made in Objective-C, in plain C++
// (docs/plans/views-migration.md, N3). ProfileContexts holds one per open
// profile, made as it opens (ApplyProfileDefaults and the fingerprint's
// languages are set on it then) and gone as it closes. On the UI thread.
class BrowserContext {
 public:
  // Holds `profile` loaded until the hold is taken (TakeKeepAlive) or this
  // goes.
  BrowserContext(std::string id, Profile* profile);
  BrowserContext(const BrowserContext&) = delete;
  BrowserContext& operator=(const BrowserContext&) = delete;
  ~BrowserContext();

  // The model's profile id, the profile's folder name.
  const std::string& id() const { return id_; }
  Profile* profile() const { return profile_; }
  base::WeakPtr<BrowserContext> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // The hold, for the engine's close or delete, which let go of it.
  std::unique_ptr<ScopedProfileKeepAlive> TakeKeepAlive();

  // MARK: History (ADR 0119)

  // Whether the profile keeps a record of where it went: the pages it visits
  // (Chrome's history, and the visited-link colours drawn from it), their
  // icons, its downloads, and what Chrome learns from its visits (the sites'
  // engagement scores, the loading predictor). Off is Chrome's
  // SavingBrowserHistoryDisabled pref, with those kept off too; the tabs,
  // their back and forward lists, cookies, and site data are unaffected. On
  // until set, and kept in the profile's prefs.
  bool SavesHistory() const;
  void SetSavesHistory(bool saves);
  // Deletes what the profile kept of where it went, for all time, as
  // Chrome's Clear browsing data does with Browsing history, Download
  // history, site usage data, and the cache: an open tab keeps its page and
  // loses its back and forward list. Cookies and site data stay, so the
  // profile stays signed in. `done` runs on the UI thread.
  void ClearHistory(base::OnceClosure done);
  // A download is under way, which a close would end.
  bool HasRunningDownloads() const;

  // MARK: Network (ADR 0037)

  // Sets the profile's proxy, as Chrome's fixed proxy rules
  // ("http://host:port", "https://…", "socks4://…", "socks5://…", the
  // "proxy" pref's fixed servers), or nullopt for the system's settings.
  // Loopback is never proxied.
  void SetProxyRules(const std::optional<std::string>& rules);
  // How WebRTC may use the network (Chrome's webrtc.ip_handling_policy):
  // "default", "default_public_interface_only",
  // "default_public_and_private_interfaces", or "disable_non_proxied_udp".
  void SetWebRTCIPHandlingPolicy(std::string_view policy);

 private:
  const std::string id_;
  const raw_ptr<Profile> profile_;
  std::unique_ptr<ScopedProfileKeepAlive> keep_alive_;
  base::WeakPtrFactory<BrowserContext> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_BROWSER_BROWSER_CONTEXT_H_
