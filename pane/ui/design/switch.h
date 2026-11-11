// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_SWITCH_H_
#define PANE_UI_DESIGN_SWITCH_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/motion.h"
#include "ui/views/controls/button/button.h"

namespace views {
class Label;
}

namespace pane {

// Pane's switch (Switch.swift; views::ToggleButton is 26 by 16): a 36-by-20
// capsule, the primary when on and the border's color when off, its 16-point
// thumb in the window's background on shadow-lg's two soft layers, 2 in from
// the track's end. A toggle crosses the thumb over and changes the track's
// color together, with Motion.toggle; a press darkens the track as a
// button's fill. The whole row is the button, as a label's click toggles a
// checkbox: a click on the label toggles it too, and so does Space. Its label
// is a line of text, or a view of its own (a title over its note, the profile
// sheet's), laid out at the row's width and as tall as it needs. Disabled,
// all of it is at 50%.
class Switch : public views::Button {
  METADATA_HEADER(Switch, views::Button)

 public:
  using Toggled = base::RepeatingCallback<void(bool on)>;

  static constexpr gfx::Size kTrackSize{36, 20};

  // `label`, when there is one, before the track, 8 points apart.
  explicit Switch(Toggled toggled, const std::u16string& label = u"");
  // `label` before the track, 8 points apart, its height for the width the
  // track leaves it; what assistive technologies read is `name`.
  Switch(Toggled toggled,
         std::unique_ptr<views::View> label,
         const std::u16string& name);
  Switch(const Switch&) = delete;
  Switch& operator=(const Switch&) = delete;
  ~Switch() override;

  // From the model: at once, as a change outside the user's toggle is.
  void SetOn(bool on);
  bool on() const { return on_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  // Its label's height is its own: a note that wraps anew grows the row.
  void ChildPreferredSizeChanged(views::View* child) override;

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  class Thumb;

  void Pressed();
  void UpdateAccessibility();
  gfx::RectF TrackBounds() const;
  // Where the thumb is now, in the switch's own coordinates.
  gfx::RectF ThumbBounds() const;

  Toggled toggled_;
  bool on_ = false;
  raw_ptr<views::View> label_ = nullptr;
  raw_ptr<Thumb> thumb_ = nullptr;
  // Where the thumb is, and how far the track's color has turned: 0 off, 1
  // on.
  ValueMotion position_;
  ControlHighlight highlight_;
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_SWITCH_H_
