// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/handed_pages.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/strings/string_util.h"
#include "components/url_formatter/url_fixer.h"

namespace pane {

bool IsHandedPage(const GURL& url) {
  return url.is_valid() && (url.SchemeIsHTTPOrHTTPS() || url.SchemeIsFile());
}

// As StartupTabProviderImpl::ParseTabFromCommandLineArg reads an argument.
std::vector<GURL> HandedPages(const base::CommandLine& command_line,
                              const base::FilePath& directory) {
  std::vector<GURL> pages;
  for (const base::CommandLine::StringType& arg : command_line.GetArgs()) {
    if (base::StartsWith(arg, FILE_PATH_LITERAL("? "))) {
      continue;
    }
    const base::FilePath path(arg);
    GURL url(path.AsUTF8Unsafe());
    // Not a URL, or a file's, whose '#' and the like the fixer reads as a
    // path's (crbug.com/40389934).
    if (!url.is_valid() || url.SchemeIsFile()) {
      url = url_formatter::FixupRelativeFile(directory, path);
    }
    if (IsHandedPage(url)) {
      pages.push_back(url);
    }
  }
  return pages;
}

}  // namespace pane
