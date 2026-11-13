// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_TEST_WAITS_H_
#define PANE_MODEL_TEST_WAITS_H_

#include "base/functional/function_ref.h"
#include "base/time/time.h"

namespace pane::model {

// Runs the test's own sequence until `condition` holds, for up to 30 s of
// real time: the thread pool works in real time, and a mock clock stays where
// it is, so a timer fires only where a test advances it.
[[nodiscard]] bool WaitFor(base::FunctionRef<bool()> condition);

// Runs the test's own sequence for `duration` of real time: for what must not
// happen meanwhile.
void RunFor(base::TimeDelta duration);

}  // namespace pane::model

#endif  // PANE_MODEL_TEST_WAITS_H_
