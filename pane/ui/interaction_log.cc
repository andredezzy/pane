// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/interaction_log.h"

#include "base/scoped_observation.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/compositor_observer.h"

namespace pane {

namespace {

// Ends one interval at the compositor's next commit, then goes.
class IntervalEnd : public ::ui::CompositorObserver {
 public:
  IntervalEnd(Interaction interaction, ::ui::Compositor* compositor)
      : interaction_(interaction),
        interval_(BeginInteractionInterval(interaction)) {
    observation_.Observe(compositor);
    // A commit, even when the input changed nothing on screen.
    compositor->ScheduleDraw();
  }

  void OnCompositingDidCommit(::ui::Compositor* compositor) override { End(); }

  void OnCompositingShuttingDown(::ui::Compositor* compositor) override {
    End();
  }

 private:
  void End() {
    EndInteractionInterval(interaction_, interval_);
    delete this;
  }

  const Interaction interaction_;
  const uint64_t interval_;
  base::ScopedObservation<::ui::Compositor, ::ui::CompositorObserver>
      observation_{this};
};

}  // namespace

void LogInteraction(Interaction interaction, ::ui::Compositor* compositor) {
  if (compositor) {
    new IntervalEnd(interaction, compositor);
  }
}

}  // namespace pane
