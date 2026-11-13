// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/test_waits.h"

#include "base/run_loop.h"
#include "base/threading/platform_thread.h"
#include "base/time/time_override.h"

namespace pane::model {

bool WaitFor(base::FunctionRef<bool()> condition) {
  for (int round = 0; round < 30'000; ++round) {
    base::RunLoop().RunUntilIdle();
    if (condition()) {
      return true;
    }
    base::PlatformThread::Sleep(base::Milliseconds(1));
  }
  return false;
}

void RunFor(base::TimeDelta duration) {
  const base::TimeTicks end =
      base::subtle::TimeTicksNowIgnoringOverride() + duration;
  while (base::subtle::TimeTicksNowIgnoringOverride() < end) {
    base::RunLoop().RunUntilIdle();
    base::PlatformThread::Sleep(base::Milliseconds(1));
  }
  base::RunLoop().RunUntilIdle();
}

}  // namespace pane::model
