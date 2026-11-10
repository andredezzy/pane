// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_AFTER_FRAME_H_
#define PANE_UI_AFTER_FRAME_H_

#include <memory>

#include "base/functional/callback_forward.h"

namespace ui {
class Compositor;
class CompositorLock;
}  // namespace ui

namespace pane {

// Holds the frames `compositor` would begin while it lives, and until the
// tasks posted before it goes have run (50 ms at most): the frame that
// answers an input is asked for once the input is handled. The display
// compositor answers a request for frames at once with a frame due at the
// next refresh, which cc drops when the UI thread reads it past that (⌘T's
// work took 3 to 5 ms of an 8.3-ms refresh), and the frame then waits for the
// refresh after, longer still when the display link stopped while the window
// was still (6 to 12 ms to its first call). Asked for once the work is done,
// it starts at once.
class FrameHold {
 public:
  explicit FrameHold(::ui::Compositor* compositor);
  FrameHold(const FrameHold&) = delete;
  FrameHold& operator=(const FrameHold&) = delete;
  ~FrameHold();

 private:
  std::unique_ptr<::ui::CompositorLock> lock_;
};

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
