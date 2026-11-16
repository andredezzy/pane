// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_RESIZE_HANDLE_H_
#define PANE_UI_SIDEBAR_RESIZE_HANDLE_H_

#include <optional>

#include "base/functional/callback.h"
#include "pane/model/app_settings.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/controls/resize_area_delegate.h"

namespace pane {

// The sidebar's edge (the Swift's SidebarResizeHandle, ADR 0061): 8 points,
// 5 over the sidebar and 3 over the panel, under every overlay. A drag
// follows the pointer between 200 and 400 points with no animation (the
// pointer leads, DESIGN.md › Motion), and is saved as it ends; a double-click
// returns to 260. The column cursor shows over it, pointing the one way left
// at either limit. Accessibility sees a splitter valued at the width, which
// steps by a tenth of its range, 20 points, as Views' sliders step: an
// assistive app's increment, and the arrows while it has the keys, which it
// takes in accessibility mode alone.
class ResizeHandle : public views::ResizeArea,
                     public views::ResizeAreaDelegate {
  METADATA_HEADER(ResizeHandle, views::ResizeArea)

 public:
  static constexpr int kWidth = 8;
  static constexpr int kOverSidebar = 5;

  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    // Each step of a drag, at the width it lays out at.
    base::RepeatingCallback<void(double width)> resize;
    // The drag ended: the width is saved.
    base::RepeatingClosure end;
    // A double-click: the standard width, saved.
    base::RepeatingClosure reset;
  };

  explicit ResizeHandle(Callbacks callbacks);
  ResizeHandle(const ResizeHandle&) = delete;
  ResizeHandle& operator=(const ResizeHandle&) = delete;
  ~ResizeHandle() override;

  // The sidebar's width as it shows.
  void SetWidth(double width);

  // views::ResizeArea:
  ui::Cursor GetCursor(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  bool HandleAccessibleAction(const ui::AXActionData& action_data) override;
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;

  // views::ResizeAreaDelegate:
  void OnResize(int resize_amount, bool done_resizing) override;

 private:
  // A tenth of the range, as Views' sliders step (Slider's
  // keyboard_increment_).
  static constexpr double kStep =
      (model::kSidebarMaxWidth - model::kSidebarMinWidth) / 10.0;

  // Steps by `delta` points and saves, as an assistive app's increment does.
  void Step(double delta);
  // The step an arrow takes, or none for another key: Right and Up widen,
  // Left and Down narrow, as a slider's.
  static std::optional<double> ArrowStep(const ui::KeyEvent& event);

  Callbacks callbacks_;
  double width_ = 0;
  // The width the drag began at, while dragging.
  std::optional<double> start_;
};

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_RESIZE_HANDLE_H_
