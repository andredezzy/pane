// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_UPDATE_PILL_H_
#define PANE_UI_SIDEBAR_UPDATE_PILL_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/sidebar/sidebar_content.h"
#include "ui/views/view.h"

namespace views {
class Button;
class Label;
}  // namespace views

namespace pane {

class IconButton;
class UpdateBar;
class UpdateDot;

// The sidebar footer's update pill (the Swift's UpdatePill, the Electron
// app's SidebarUpdatePill): quiet by design, no color beyond its status dot,
// 32 points tall on the foreground at 5%, rounded 15. Its click downloads,
// then restarts or quits to install; the dismiss button, which shows under
// the pointer, hides it for this version until relaunch (DESIGN.md ›
// Updates). While the dmg downloads, the dot pulses and a bar after the text
// runs out to 7 from the pill's end, as the dot sits 7 from its start; under
// the pointer the bar draws back at once, 25 from its end, within room it
// always keeps so the text never moves, and the dismiss button shows once
// the bar has cleared it; on the way out the button goes first
// (Motion.hoverTurn). The text crossfades as the status changes.
class UpdatePill : public views::View {
  METADATA_HEADER(UpdatePill, views::View)

 public:
  static constexpr int kHeight = 32;

  UpdatePill(base::RepeatingClosure press, base::RepeatingClosure dismiss);
  UpdatePill(const UpdatePill&) = delete;
  UpdatePill& operator=(const UpdatePill&) = delete;
  ~UpdatePill() override;

  void Set(const SidebarUpdate& update);
  // Its click and its dismiss, for the channel.
  const views::View* button() const;
  const views::View* dismiss() const;
  // Comes in, fading and rising 4 points, or goes the same way back, on
  // Motion.fade; `gone` runs once it has.
  void Show(bool shown, base::OnceClosure gone);
  bool shown() const { return shown_; }

  // views::View:
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  class Press;

  ui::Compositor* compositor();
  void SetHovered(bool hovered);
  void Appeared(double share);

  SidebarUpdate update_;
  bool hovered_ = false;
  bool shown_ = false;
  base::OnceClosure gone_;

  raw_ptr<Press> button_ = nullptr;
  // The dot breathes while the dmg downloads.
  raw_ptr<UpdateDot> dot_ = nullptr;
  // The status line, and the one it replaces while they crossfade.
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::Label> leaving_label_ = nullptr;
  raw_ptr<UpdateBar> bar_ = nullptr;
  raw_ptr<IconButton> dismiss_ = nullptr;

  ValueMotion appear_;
  ValueMotion label_in_;
  ValueMotion bar_shown_;
  // How far the bar has drawn back from the pill's end, 0 to 25 points.
  ValueMotion bar_back_;
  ValueMotion dismiss_shown_;
};

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_UPDATE_PILL_H_
