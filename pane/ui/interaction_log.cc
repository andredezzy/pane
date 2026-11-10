// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/interaction_log.h"

#include "base/functional/bind.h"
#include "components/viz/common/frame_timing_details.h"
#include "ui/compositor/compositor.h"

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
        EndInteractionInterval(interaction, interval);
      },
      interaction, interval));
  // A frame, even when nothing changed on the screen.
  compositor->ScheduleDraw();
}

}  // namespace pane
