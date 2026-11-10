// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_AFTER_FRAME_H_
#define PANE_UI_AFTER_FRAME_H_

#include "base/functional/callback_forward.h"

namespace ui {
class Compositor;
}

namespace pane {

// Runs `work` once the frame `compositor` builds next has left the UI
// thread, drawn and handed to the display compositor, so work that would hold
// that frame back, as the engine making a page or hiding one does, comes
// after the frame that answers an input: the Swift app's AfterFrame, which
// waited for Core Animation's commit, a frame sooner than the screen. When
// `compositor` draws nothing (its window hidden), or no frame comes in time
// (the display asleep), it runs anyway.
void AfterFrame(::ui::Compositor* compositor, base::OnceClosure work);

}  // namespace pane

#endif  // PANE_UI_AFTER_FRAME_H_
