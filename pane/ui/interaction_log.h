// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_INTERACTION_LOG_H_
#define PANE_UI_INTERACTION_LOG_H_

#include <cstdint>

#include "base/time/time.h"

namespace ui {
class Compositor;
}

namespace pane {

// The interactions interaction-timing times, by the names the Swift app
// gave them, and what the Swift didn't time: a click into the address, the
// tab switcher's first frame, a tab without a page woken to its page's first
// paint, Settings shown, a Theme chosen, a menu's trigger pressed (its
// frame, up to the system's menu, which draws in a window of its own), the
// App icon picker opened and a sky chosen in it, Quit Pane? asked by ⌘Q,
// and a page's dialog and a permission prompt shown as the page asks, from
// Chrome's hook (ADR 0021).
enum class Interaction {
  kSelectTab,
  kNewTab,
  kFocusAddressField,
  kClickAddressField,
  kOpenProfileSheet,
  kChoosePlatform,
  kOpenSwitcher,
  kWakeTab,
  kOpenSettings,
  kChangeTheme,
  kOpenMenu,
  kOpenAppIconPicker,
  kChooseAppIcon,
  kOpenQuitDialog,
  kShowPageDialog,
  kShowPermissionPrompt,
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

// How long before an interval's end, which the UI thread hears, the frame it
// answers with went out: `committed` to Core Animation, the frame handed to
// the window server, where the Swift app's intervals ended; `presented` to
// the screen, as the GPU process predicts it from its display link, which
// can still be ahead (negative). Zero where the frame said neither.
struct FrameTimes {
  base::TimeDelta committed;
  base::TimeDelta presented;
};

// The platform's half: an interval's begin and end, in the system's trace,
// with when its frame went out, which the trace's reader takes off
// (tests/lib/signposts.ts): the end is heard on the UI thread, which work
// after the frame can hold past it (a new tab's page is made right after the
// frame that answers ⌘T).
uint64_t BeginInteractionInterval(Interaction interaction);
void EndInteractionInterval(Interaction interaction,
                            uint64_t interval,
                            const FrameTimes& frame = FrameTimes());

}  // namespace pane

#endif  // PANE_UI_INTERACTION_LOG_H_
