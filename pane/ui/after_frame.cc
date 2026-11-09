// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/after_frame.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "components/viz/common/frame_timing_details.h"
#include "ui/compositor/compositor.h"

namespace pane {

namespace {

// Past a frame's usual time to the screen, at 60 Hz, several times over.
constexpr base::TimeDelta kLongestWait = base::Milliseconds(100);

using Work = base::RefCountedData<base::OnceClosure>;

// The first of the frame and the wait runs the work; the other finds it gone.
void RunOnce(scoped_refptr<Work> work) {
  if (work->data) {
    std::move(work->data).Run();
  }
}

}  // namespace

void AfterFrame(::ui::Compositor* compositor, base::OnceClosure work) {
  scoped_refptr<base::SequencedTaskRunner> runner =
      base::SequencedTaskRunner::GetCurrentDefault();
  if (!compositor || !compositor->IsVisible()) {
    runner->PostTask(FROM_HERE, std::move(work));
    return;
  }
  auto once = base::MakeRefCounted<Work>(std::move(work));
  // Posted, not run in the compositor's own callback, which other frame
  // callbacks share.
  compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce(
      [](scoped_refptr<base::SequencedTaskRunner> runner,
         scoped_refptr<Work> once, const viz::FrameTimingDetails&) {
        runner->PostTask(FROM_HERE, base::BindOnce(&RunOnce, once));
      },
      runner, once));
  runner->PostDelayedTask(FROM_HERE, base::BindOnce(&RunOnce, once),
                          kLongestWait);
  compositor->ScheduleDraw();
}

}  // namespace pane
