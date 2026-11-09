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
  const uint64_t interval = BeginInteractionInterval(interaction);
  // The frame built after the input, once it is on the screen.
  compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce(
      [](Interaction interaction, uint64_t interval,
         const viz::FrameTimingDetails& details) {
        EndInteractionInterval(interaction, interval);
      },
      interaction, interval));
  // A frame, even when the input changed nothing on the screen.
  compositor->ScheduleDraw();
}

}  // namespace pane
