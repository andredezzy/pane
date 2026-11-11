// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_EXTENSIONS_INSTALLED_EXTENSIONS_H_
#define PANE_UI_EXTENSIONS_INSTALLED_EXTENSIONS_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "extensions/browser/extension_action_icon_factory.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

class Profile;

namespace content {
class WebContents;
}

namespace extensions {
class Extension;
class ExtensionAction;
}  // namespace extensions

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
  // Whether it has a toolbar action, and its title for the tab; empty
  // without one.
  bool has_action = false;
  std::string action_title;
  // The action shows for the tab (chrome.action.enable and disable, and a
  // page action's declarative show): its button dims to 40% when it doesn't.
  bool action_enabled = false;
  // The badge for the tab, and its colors, which keep their alpha: an unset
  // one is transparent.
  std::string badge_text;
  SkColor badge_background = SK_ColorTRANSPARENT;
  SkColor badge_text_color = SK_ColorTRANSPARENT;
  // Its options page, when it has one.
  GURL options_url;
  // The action's icon, as Chrome's toolbar draws it: none for an extension
  // that is off or has no action. It can be there and blank, as an icon
  // whose file has yet to load is.
  gfx::Image icon;
};

// A profile's action icons, each extension's factory kept so an icon still
// loading tells its observer once it has (ADR 0031). Chrome's factory keeps
// the first default icon it returns, a blank placeholder when asked before
// the file loaded, so once any icon has loaded the factories start over, at
// the next ask, outside their own callback.
class ActionIcons : public extensions::ExtensionActionIconFactory::Observer {
 public:
  // `loaded` runs once an icon has loaded, to ask for it again.
  explicit ActionIcons(base::RepeatingClosure loaded);
  ActionIcons(const ActionIcons&) = delete;
  ActionIcons& operator=(const ActionIcons&) = delete;
  ~ActionIcons() override;

  // The icon of `extension`'s `action` for the tab, as Chrome's toolbar
  // chooses it: the one set for the tab, else its declarative one, else its
  // default.
  gfx::Image IconFor(const extensions::Extension& extension,
                     extensions::ExtensionAction& action,
                     int tab_id);
  // An extension that went (unloaded, uninstalled).
  void Forget(const std::string& extension_id);

  // extensions::ExtensionActionIconFactory::Observer:
  void OnIconUpdated() override;

 private:
  std::map<std::string, std::unique_ptr<extensions::ExtensionActionIconFactory>>
      factories_;
  bool loaded_ = false;
  base::RepeatingClosure loaded_callback_;
};

// What `profile` has installed, on or off, as Chrome's extensions page lists
// it, in the order of their names; each action as it is for `tab`, a page of
// the profile, or for every tab without one. `icons` keeps the profile's
// icon factories; without it each icon is asked of a factory made for it.
std::vector<InstalledExtension> InstalledExtensions(Profile* profile,
                                                    content::WebContents* tab,
                                                    ActionIcons* icons);

}  // namespace pane

#endif  // PANE_UI_EXTENSIONS_INSTALLED_EXTENSIONS_H_
