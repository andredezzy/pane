// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_HANDED_PAGES_H_
#define PANE_UI_APP_HANDED_PAGES_H_

#include <vector>

#include "url/gurl.h"

namespace base {
class CommandLine;
class FilePath;
}  // namespace base

namespace pane {

// Whether another app may hand Pane `url` to open (ADR 0109): a web page
// (http, https) or a file. Nothing else reaches the engine.
bool IsHandedPage(const GURL& url);

// The web pages and files a launch of Pane names, as another app hands them:
// each argument of `command_line` read as Chrome reads a launch's own (a URL,
// or a file's path relative to `directory`, url_formatter's
// FixupRelativeFile), and only what IsHandedPage takes kept, in order.
// Search terms ("? words") are left out: no profile's search engine reads
// them. It reads the disk, for a relative file: off the UI thread.
std::vector<GURL> HandedPages(const base::CommandLine& command_line,
                              const base::FilePath& directory);

}  // namespace pane

#endif  // PANE_UI_APP_HANDED_PAGES_H_
