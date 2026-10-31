// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_ENGINE_TRACE_H_
#define PANE_BROWSER_ENGINE_TRACE_H_

#include <string_view>

#include "base/values.h"

namespace pane {

// Records the traffic between the Pane app and the engine: one JSON line per
// embedding-API call (`kind` "call") and per delegate callback (`kind`
// "callback"), appended to the file named by the PANE_ENGINE_TRACE
// environment variable. Scenarios assert against this file.
//
// Without the variable, tracing costs one branch: call sites go through
// PANE_TRACE (or test IsTracing() themselves), which builds a line's
// arguments only while tracing.
// A scenario's launch (the mock keychain) allows the trace, which names the
// pages: before the first trace call, since Chrome's command line is not
// there yet.
void AllowTracing();

bool IsTracing();

void Trace(std::string_view kind,
           std::string_view name,
           base::DictValue args = {});

}  // namespace pane

// Traces one line. `args`, a base::DictValue, is evaluated only while
// tracing, so the strings and lookups that build it cost nothing otherwise.
#define PANE_TRACE(kind, name, ...)                         \
  do {                                                      \
    if (::pane::IsTracing()) {                              \
      ::pane::Trace(kind, name __VA_OPT__(, ) __VA_ARGS__); \
    }                                                       \
  } while (0)

#endif  // PANE_BROWSER_ENGINE_TRACE_H_
