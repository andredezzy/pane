// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/after_frame.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/scoped_observation.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/compositor_lock.h"
#include "ui/compositor/compositor_observer.h"

namespace pane {

namespace {

// Past a frame's usual time, at 60 Hz, several times over.
constexpr base::TimeDelta kLongestWait = base::Milliseconds(100);

// Past any input's own work many times over (⌘T's is 3 ms): a hold left on
// by an input that runs a nested loop (a system panel) lets frames go then.
constexpr base::TimeDelta kLongestHold = base::Milliseconds(50);

// Waits for the frame `compositor` builds from now: one that takes a commit
// made after the wait began, once it is handed to the display compositor
// (OnCompositingStarted, which follows the commit and the draw), as the
// Swift's waited for Core Animation's commit. The work then runs as a task
// of its own, so it holds no frame back. A frame that never comes (the
// display asleep, the compositor going) gives way after kLongestWait.
class FrameWaiter : public ui::CompositorObserver {
 public:
  FrameWaiter(ui::Compositor* compositor, base::OnceClosure work)
      : work_(std::move(work)) {
    observation_.Observe(compositor);
    timeout_.Start(
        FROM_HERE, kLongestWait,
        base::BindOnce(&FrameWaiter::Finish, base::Unretained(this)));
    compositor->ScheduleDraw();
  }

  FrameWaiter(const FrameWaiter&) = delete;
  FrameWaiter& operator=(const FrameWaiter&) = delete;
  ~FrameWaiter() override = default;

  // ui::CompositorObserver:
  void OnCompositingDidCommit(ui::Compositor* compositor) override {
    committed_ = true;
  }

  void OnCompositingStarted(ui::Compositor* compositor,
                            base::TimeTicks start_time) override {
    if (committed_) {
      Finish();
    }
  }

  void OnCompositingShuttingDown(ui::Compositor* compositor) override {
    Finish();
  }

 private:
  // Deletes this.
  void Finish() {
    observation_.Reset();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             std::move(work_));
    delete this;
  }

  base::OnceClosure work_;
  bool committed_ = false;
  base::OneShotTimer timeout_;
  base::ScopedObservation<ui::Compositor, ui::CompositorObserver> observation_{
      this};
};

}  // namespace

FrameHold::FrameHold(::ui::Compositor* compositor)
    : lock_(compositor ? compositor->GetCompositorLock(/*client=*/nullptr,
                                                       kLongestHold)
                       : nullptr) {}

FrameHold::~FrameHold() {
  if (!lock_) {
    return;
  }
  // After the tasks the input posted (a row brought into view, a layout),
  // which the frame then holds too.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::DoNothingWithBoundArgs(std::move(lock_)));
}

void AfterFrame(::ui::Compositor* compositor, base::OnceClosure work) {
  if (!compositor || !compositor->IsVisible()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             std::move(work));
    return;
  }
  // Owns itself until its frame comes.
  new FrameWaiter(compositor, std::move(work));
}

}  // namespace pane
