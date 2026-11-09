// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_FIELD_H_
#define PANE_UI_DESIGN_FIELD_H_

#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/text.h"
#include "ui/color/color_variant.h"
#include "ui/views/controls/textfield/textfield.h"

namespace pane {

class FocusRing;

// Pane's text field (Fields.swift's PaneTextField): Chromium's Textfield,
// which brings IME, undo, Cut/Copy/Paste, Services, and accessibility, drawn
// as packages/ui's Input. It is 36 points tall, rounded 11, filled (the
// window's background unless given another), its 1-point border inside the
// edge, its text 13 points in (px-3 inside the border), 14 points by
// default, the placeholder muted, with no hover of its own. Focused, the ring
// shows outside the border; a problem shows alone, as the border turning red
// (memory field-problem-red-ring), coming and going with Motion.toggle,
// which VoiceOver reads as the field's description. Disabled, all of it is at
// 50%.
class Field : public views::Textfield {
  METADATA_HEADER(Field, views::Textfield)

 public:
  static constexpr int kHeight = 36;
  static constexpr float kRadius = 11;
  // From the outer edge: CSS's px-3 inside the 1-point border.
  static constexpr int kTextInset = 13;

  explicit Field(ui::ColorVariant fill = kPaneColorWindowBackground,
                 const TextStyle& style = text::kField);
  Field(const Field&) = delete;
  Field& operator=(const Field&) = delete;
  ~Field() override;

  // Why it keeps a form from saving, or nullopt.
  void SetProblem(std::optional<std::u16string> problem);
  bool has_problem() const { return problem_.has_value(); }

  // views::Textfield:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void OnPaintBorder(gfx::Canvas* canvas) override;

 private:
  void EnabledChanged();

  ui::ColorVariant fill_;
  std::optional<std::u16string> problem_;
  // How far the border has turned red, 0 to 1.
  ValueMotion problem_shown_;
  raw_ptr<FocusRing> ring_ = nullptr;
  bool made_layer_ = false;
  base::CallbackListSubscription enabled_subscription_;
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_FIELD_H_
