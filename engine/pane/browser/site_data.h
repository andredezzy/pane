// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_SITE_DATA_H_
#define PANE_BROWSER_SITE_DATA_H_

#include <stdint.h>

#include <vector>

#include "base/functional/callback_forward.h"
#include "url/origin.h"

namespace content {
class BrowserContext;
}

namespace pane {

// Removes `remove_mask` (content::BrowsingDataRemover data types) that
// `origins` stored, or that every site stored when `origins` is empty, then
// runs `done` on the UI thread.
void RemoveSiteData(content::BrowserContext* browser_context,
                    uint64_t remove_mask,
                    const std::vector<url::Origin>& origins,
                    base::OnceClosure done);

}  // namespace pane

#endif  // PANE_BROWSER_SITE_DATA_H_
