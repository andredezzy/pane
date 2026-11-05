// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_CACHES_IN_MEMORY_H_
#define PANE_BROWSER_CACHES_IN_MEMORY_H_

#include <string>

namespace content {
class BrowserContext;
}

namespace pane {

// The profiles, by identifier (the directory's name), whose HTTP and code
// caches stay off the disk. UI thread only. Set before the profile opens: its
// network context and its code cache read this once, as they are created.
void SetKeepsCachesInMemory(const std::string& identifier, bool in_memory);

bool KeepsCachesInMemory(content::BrowserContext* browser_context);

}  // namespace pane

#endif  // PANE_BROWSER_CACHES_IN_MEMORY_H_
