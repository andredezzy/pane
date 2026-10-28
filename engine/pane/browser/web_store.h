// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_WEB_STORE_H_
#define PANE_BROWSER_WEB_STORE_H_

namespace content {
class NavigationHandle;
}

namespace pane {

// On the Chrome Web Store, the item's install and remove buttons say Pane
// where the store says Chrome ("Add to Pane", "Remove from Pane", in each of
// the store's languages). The store renders that text itself, client-side,
// so each of its documents gets a script, as it commits, in an isolated
// world of Pane's: it renames those two buttons' labels as the store draws
// and redraws them, and touches nothing else on the page or on any other
// site.
void RenameWebStoreButtons(content::NavigationHandle* navigation_handle);

}  // namespace pane

#endif  // PANE_BROWSER_WEB_STORE_H_
