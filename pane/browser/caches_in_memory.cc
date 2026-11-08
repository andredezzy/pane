// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/caches_in_memory.h"

#include <set>

#include "base/files/file_path.h"
#include "base/no_destructor.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"

namespace pane {

namespace {

std::set<std::string>& Identifiers() {
  static base::NoDestructor<std::set<std::string>> identifiers;
  return *identifiers;
}

}  // namespace

void SetKeepsCachesInMemory(const std::string& identifier, bool in_memory) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (in_memory) {
    Identifiers().insert(identifier);
  } else {
    Identifiers().erase(identifier);
  }
}

bool KeepsCachesInMemory(content::BrowserContext* browser_context) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  return Identifiers().contains(browser_context->GetPath().BaseName().AsUTF8Unsafe());
}

}  // namespace pane
