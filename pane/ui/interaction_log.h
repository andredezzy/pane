// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_INTERACTION_LOG_H_
#define PANE_UI_INTERACTION_LOG_H_

#include <cstdint>

namespace ui {
class Compositor;
}

namespace pane {

// The interactions interaction-timing times, by the names the Swift app
// gave them.
enum class Interaction {
  kSelectTab,
  kNewTab,
  kFocusAddressField,
};

// Times `interaction` from now, as its input is handled, to the commit of
// the frame `compositor` builds next, the point the Swift app's interval
// ended at (Core Animation's commit, AfterFrame): an interval in the
// system's trace (Instruments' Points of Interest on the Mac).
void LogInteraction(Interaction interaction, ::ui::Compositor* compositor);

// The platform's half: an interval's begin and end, in the system's trace.
uint64_t BeginInteractionInterval(Interaction interaction);
void EndInteractionInterval(Interaction interaction, uint64_t interval);

}  // namespace pane

#endif  // PANE_UI_INTERACTION_LOG_H_
