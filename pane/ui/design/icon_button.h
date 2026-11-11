// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_ICON_BUTTON_H_
#define PANE_UI_DESIGN_ICON_BUTTON_H_

#include <memory>
#include <string>

#include "pane/ui/design/controls.h"
#include "pane/ui/design/icons.h"
#include "ui/views/controls/button/button.h"

namespace pane {

// An icon button (the Swift's IconButtonStyle): the icon centered in a
// circle `size` points across, muted, turning to the accent's foreground as
// the accent fades in under it (Motion.hover); a press at once; 40% while
// disabled. `name` is what assistive technologies read, and its tooltip, as
// the Swift's .help. Another look takes another shape or none of the fill,
// as the update pill's dismiss (a 4-point squircle, its tint alone lit).
class IconButton : public views::Button {
  METADATA_HEADER(IconButton, views::Button)

 public:
  // Lucide's icon at `icon_size` points, each line `stroke_width` of its
  // 24-unit grid.
  struct Glyph {
    Icon icon;
    int size = 16;
    float stroke_width = 2;
  };

  IconButton(PressedCallback pressed,
             int size,
             const Glyph& glyph,
             const std::u16string& name,
             const ControlLook& look = IconButtonLook());
  IconButton(const IconButton&) = delete;
  IconButton& operator=(const IconButton&) = delete;
  ~IconButton() override;

  // Another icon, as Reload turns to Stop, and the name it reads then.
  void SetGlyph(const Glyph& glyph);
  void SetName(const std::u16string& name);
  // Another look, as an extension's pin turns blue once pinned.
  void SetLook(const ControlLook& look);
  // Lit while what it opens shows (the puzzle's menu), as IconButtonStyle's
  // isOn; once that goes, the pointer's place decides.
  void SetHeld(bool held);

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  const int size_;
  Glyph glyph_;
  ControlLook look_;
  std::unique_ptr<ControlHighlight> highlight_;
  raw_ptr<ControlBackground> background_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_ICON_BUTTON_H_
