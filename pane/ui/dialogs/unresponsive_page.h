// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DIALOGS_UNRESPONSIVE_PAGE_H_
#define PANE_UI_DIALOGS_UNRESPONSIVE_PAGE_H_

#include "base/functional/callback_forward.h"

namespace content {
class RenderWidgetHost;
class WebContents;
}  // namespace content

namespace pane {

// A page that stopped answering (Chrome's hang monitor), asked about in the
// window's own dialog (ADR 0021): "Page Unresponsive", Wait or Exit Page.
// Wait, and Esc, keep it and start the monitor again, as Chrome's dialog
// closed does; Exit Page ends its renderer, and the tab shows its crash. One
// per page while it shows; the page answering again takes it back. Pane's
// Browsers' delegate calls them (HostedWebContentsDelegate).
void AskAboutUnresponsivePage(content::WebContents* web_contents,
                              content::RenderWidgetHost* render_widget_host,
                              base::RepeatingClosure hang_monitor_restarter);
void PageResponsiveAgain(content::WebContents* web_contents);

}  // namespace pane

#endif  // PANE_UI_DIALOGS_UNRESPONSIVE_PAGE_H_
