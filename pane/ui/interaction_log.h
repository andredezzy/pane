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
// Chrome's hook (ADR 0021); find in page's bar shown by ⌘F, and a step to
// another match (Return, a chevron) to the frame showing its count (ADR
// 0067); the page's context menu, from Chrome's hook to the system's menu
// tracking (the menu draws in a window of its own); and DevTools opened, to
// the frame that shows their first paint (ADR 0024); the toolbar's puzzle to
// the frame showing the extensions menu (ADR 0054); an action's press, or
// chrome.action.openPopup, to the first frame of its popup, which shows once
// its page has loaded, in a window of its own (ADR 0031); a prompt that adds
// or removes an extension, from Chrome's ask or the menu's press to its
// frame (ADR 0056); and Chrome's extensions page opened in the profile's
// tab, to the frame that shows the tab, and on to the page's first paint
// (D3).
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
  kShowFindBar,
  kFindStep,
  kShowContextMenu,
  kOpenDevTools,
  kOpenExtensionsMenu,
  kShowExtensionPopup,
  kShowExtensionPrompt,
  kOpenExtensionsPage,
  kPaintExtensionsPage,
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
