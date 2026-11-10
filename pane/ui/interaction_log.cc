// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/interaction_log.h"

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "components/viz/common/frame_timing_details.h"
#include "ui/compositor/compositor.h"
#include "ui/gfx/presentation_feedback.h"

namespace pane {

void LogInteraction(Interaction interaction, ::ui::Compositor* compositor) {
  if (!compositor) {
    return;
  }
  EndInteractionAtNextFrame(interaction, BeginInteractionInterval(interaction),
                            compositor);
}

void EndInteractionAtNextFrame(Interaction interaction,
                               uint64_t interval,
                               ::ui::Compositor* compositor) {
  if (!compositor) {
    return;
  }
  // The frame built after now, once it is on the screen.
  compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce(
      [](Interaction interaction, uint64_t interval,
         const viz::FrameTimingDetails& details) {
        const base::TimeTicks now = base::TimeTicks::Now();
        const gfx::PresentationFeedback& feedback =
            details.presentation_feedback;
        FrameTimes frame;
        // On the Mac the latch is the frame's commit to Core Animation
        // (CALayerTreeCoordinator::CommitPresentedFrameToCA).
        if (!feedback.latch_timestamp.is_null()) {
          frame.committed = now - feedback.latch_timestamp;
        }
        if (!feedback.timestamp.is_null()) {
          frame.presented = now - feedback.timestamp;
        }
        EndInteractionInterval(interaction, interval, frame);
      },
      interaction, interval));
  // A frame, even when nothing changed on the screen.
  compositor->ScheduleDraw();
}

}  // namespace pane
