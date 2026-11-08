// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_TABS_TAB_ID_H_
#define PANE_UI_TABS_TAB_ID_H_

#include <string>

namespace content {
class WebContents;
}

namespace pane {

// A tab's id, for Pane's UI and the automation channel: a UUID, made when
// first asked, for as long as its WebContents lives.
const std::string& TabIdOf(content::WebContents* contents);

// The live tab with that id, or null.
content::WebContents* TabWithId(const std::string& id);

}  // namespace pane

#endif  // PANE_UI_TABS_TAB_ID_H_
