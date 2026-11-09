// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_AFTER_FRAME_H_
#define PANE_UI_AFTER_FRAME_H_

#include "base/functional/callback_forward.h"

namespace ui {
class Compositor;
}

namespace pane {

// Runs `work` once the frame `compositor` builds next is on the screen, so
// work that would hold that frame back, as the engine making a page does,
// comes after the frame that answers an input: the Swift app's AfterFrame.
// When `compositor` draws nothing (its window hidden or occluded), or no
// frame comes in time (the display asleep), it runs anyway.
void AfterFrame(::ui::Compositor* compositor, base::OnceClosure work);

}  // namespace pane

#endif  // PANE_UI_AFTER_FRAME_H_
