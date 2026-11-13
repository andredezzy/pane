// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_SITE_DATA_H_
#define PANE_BROWSER_SITE_DATA_H_

#include <stdint.h>

#include "base/functional/callback_forward.h"

namespace content {
class BrowserContext;
}

namespace pane {

// Removes `remove_mask` (content::BrowsingDataRemover data types) that every
// site stored, then runs `done` on the UI thread.
void RemoveSiteData(content::BrowserContext* browser_context,
                    uint64_t remove_mask,
                    base::OnceClosure done);

}  // namespace pane

#endif  // PANE_BROWSER_SITE_DATA_H_
