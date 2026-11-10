// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_INTERACTION_LOG_H_
#define PANE_UI_INTERACTION_LOG_H_

#include <cstdint>

namespace ui {
class Compositor;
}

namespace pane {

// The interactions interaction-timing times, by the names the Swift app
// gave them, and what the Swift didn't time: a click into the address, the
// tab switcher's first frame, and a tab without a page woken to its page's
// first paint.
enum class Interaction {
  kSelectTab,
  kNewTab,
  kFocusAddressField,
  kClickAddressField,
  kOpenProfileSheet,
  kChoosePlatform,
  kOpenSwitcher,
  kWakeTab,
};

// Times `interaction` from now, as its input is handled, to the frame
// `compositor` builds next reaching the screen: an interval in the system's
// trace (Instruments' Points of Interest on the Mac). The Swift app's ended
// at Core Animation's commit (AfterFrame), before its frame was on the
// screen, so these run longer by that much for the same work.
void LogInteraction(Interaction interaction, ::ui::Compositor* compositor);

// Ends an interval begun earlier (BeginInteractionInterval) once the frame
// `compositor` builds next is on the screen.
void EndInteractionAtNextFrame(Interaction interaction,
                               uint64_t interval,
                               ::ui::Compositor* compositor);

// The platform's half: an interval's begin and end, in the system's trace.
uint64_t BeginInteractionInterval(Interaction interaction);
void EndInteractionInterval(Interaction interaction, uint64_t interval);

}  // namespace pane

#endif  // PANE_UI_INTERACTION_LOG_H_
