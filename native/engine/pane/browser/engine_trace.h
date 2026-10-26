// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_ENGINE_TRACE_H_
#define PANE_BROWSER_ENGINE_TRACE_H_

#include <string_view>

#include "base/values.h"

namespace pane {

// Records the traffic between the Pane app and the engine: one JSON line per
// embedding-API call (`kind` "call") and per delegate callback (`kind`
// "callback"), appended to the file named by the PANE_ENGINE_TRACE
// environment variable. Scenarios assert against this file. Without the
// variable, tracing costs one branch.
void Trace(std::string_view kind,
           std::string_view name,
           base::DictValue args = {});

}  // namespace pane

#endif  // PANE_BROWSER_ENGINE_TRACE_H_
