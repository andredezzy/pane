// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_ENGINE_TRACE_H_
#define PANE_BROWSER_ENGINE_TRACE_H_

#include <string_view>

#include "base/values.h"

namespace pane {

// Records what Pane does for the scenarios to check: one JSON line per event
// (`kind` "call" for what Pane asks of the engine, "callback" for what the
// engine tells it), appended to the file the PANE_ENGINE_TRACE environment
// variable names, in a scenario's run alone (model::TestHook): the trace
// names the pages.
//
// Without the variable, tracing costs one branch: call sites go through
// PANE_TRACE (or test IsTracing() themselves), which builds a line's
// arguments only while tracing.
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
