// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/app_callback.h"

#include "base/check_op.h"

namespace pane {

namespace {

// Main thread only, as every call between the app and the engine.
int g_depth = 0;

}  // namespace

ScopedAppCallback::ScopedAppCallback() {
  ++g_depth;
}

ScopedAppCallback::~ScopedAppCallback() {
  DCHECK_GT(g_depth, 0);
  --g_depth;
}

bool InAppCallback() {
  return g_depth > 0;
}

}  // namespace pane
