// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_BROWSER_MAIN_EXTRA_PARTS_H_
#define PANE_BROWSER_BROWSER_MAIN_EXTRA_PARTS_H_

#include "base/callback_list.h"
#include "base/scoped_observation.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_manager_observer.h"

namespace pane {

// Tells the Pane app when a hosted engine is ready, and releases the app's
// profiles and pages before Chrome tears profiles down. Process lifetime needs
// nothing extra: Chrome's AppController already keeps the browser process
// alive without windows on macOS, and releases that keep-alive when quitting.
// Also brings up, for each profile as it is added, the services Chrome starts
// through parts Pane does not run (sync), and hands the certificate verifier
// the CRLSet the bundle carries.
class BrowserMainExtraParts : public ChromeBrowserMainExtraParts,
                              public ProfileManagerObserver {
 public:
  BrowserMainExtraParts();
  BrowserMainExtraParts(const BrowserMainExtraParts&) = delete;
  BrowserMainExtraParts& operator=(const BrowserMainExtraParts&) = delete;
  ~BrowserMainExtraParts() override;

  // ChromeBrowserMainExtraParts:
  void PreProfileInit() override;
  void PostProfileInit(Profile* profile, bool is_initial_profile) override;
  void PostBrowserStart() override;
  void PostMainMessageLoopRun() override;

  // ProfileManagerObserver:
  void OnProfileAdded(Profile* profile) override;
  void OnProfileManagerDestroying() override;

 private:
  base::ScopedObservation<ProfileManager, ProfileManagerObserver>
      profile_manager_observation_{this};
  base::CallbackListSubscription app_terminating_subscription_;
};

}  // namespace pane

#endif  // PANE_BROWSER_BROWSER_MAIN_EXTRA_PARTS_H_
