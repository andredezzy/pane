// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_INSTALLED_EXTENSIONS_H_
#define PANE_UI_APP_INSTALLED_EXTENSIONS_H_

#include <string>
#include <vector>

#include "ui/gfx/image/image.h"

class Profile;

namespace content {
class WebContents;
}

namespace pane {

// An extension the user installed in a profile, as Chromium holds it: its
// registry says whether it is on, its pins are the toolbar model's (the
// profile's prefs), and its action is the action manager's, as it is for one
// tab (the Swift app's PaneExtension).
struct InstalledExtension {
  InstalledExtension();
  InstalledExtension(const InstalledExtension&);
  InstalledExtension& operator=(const InstalledExtension&);
  ~InstalledExtension();

  std::string id;
  std::string name;
  std::string version;
  bool enabled = false;
  bool pinned = false;
  // The action's title; empty without an action.
  std::string action_title;
  // The action's icon, as Chrome's toolbar draws it: none for an extension
  // that is off or has no action. It can be there and blank, as an icon
  // whose file has yet to load is.
  gfx::Image icon;
};

// What `profile` has installed, on or off, as Chrome's extensions page lists
// it, in the order of their names; each action as it is for `tab`, a page of
// the profile, or for every tab without one.
std::vector<InstalledExtension> InstalledExtensions(Profile* profile,
                                                    content::WebContents* tab);

}  // namespace pane

#endif  // PANE_UI_APP_INSTALLED_EXTENSIONS_H_
