// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_LOCK_LOCK_UPDATE_STATUS_H_
#define PANE_UI_LOCK_LOCK_UPDATE_STATUS_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "pane/ui/design/motion.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace pane {

class UpdateBar;
class UpdateDot;

// The update on offer while Pane is locked (the Swift's LockUpdateStatus;
// Paper 07.1, 06.3.1; DESIGN.md › Updates): the dot, the status at 12 in the
// accent's foreground, and while the dmg downloads its bar, 64 wide, and the
// time left, muted, at least 56 wide; under the pointer the update's action
// fades in beside them, 12 medium and muted (Motion.hover): Download, Restart
// once the new version is staged, or Quit where Pane can't install it itself.
// The action's width is kept on the other side too, so the status stays in
// the middle and still, and nothing wraps.
class LockUpdateStatus : public views::View {
  METADATA_HEADER(LockUpdateStatus, views::View)

 public:
  struct Shows {
    friend bool operator==(const Shows&, const Shows&) = default;

    std::u16string label;
    bool downloading = false;
    double progress = 0;
    // The time left, as the Swift wrote it ("~2min"), once known.
    std::u16string eta;
    // The action's title, and the view id the Swift named it by; none while
    // it downloads.
    std::u16string action;
    int action_id = 0;
  };

  explicit LockUpdateStatus(base::RepeatingClosure action);
  LockUpdateStatus(const LockUpdateStatus&) = delete;
  LockUpdateStatus& operator=(const LockUpdateStatus&) = delete;
  ~LockUpdateStatus() override;

  void Set(const Shows& shows);
  // The action, while there is one, for the channel.
  const views::View* action() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  class Action;

  // The action's width, kept on both sides.
  int ActionWidth() const;
  void SetHovered(bool hovered);

  Shows shows_;
  raw_ptr<UpdateDot> dot_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<UpdateBar> bar_ = nullptr;
  raw_ptr<views::Label> eta_ = nullptr;
  raw_ptr<Action> action_ = nullptr;
  bool hovered_ = false;
  ValueMotion action_shown_;
};

}  // namespace pane

#endif  // PANE_UI_LOCK_LOCK_UPDATE_STATUS_H_
