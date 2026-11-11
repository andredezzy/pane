// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_FIELD_H_
#define PANE_UI_DESIGN_FIELD_H_

#include <memory>
#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/text.h"
#include "ui/color/color_variant.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget_observer.h"

namespace pane {

class FocusRing;

// A field's selection as the platform's own text views draw theirs: the
// system's highlight while the field's window has the keys, and its
// unemphasized one while it doesn't, under the field's own text color (an
// NSTextField's, which the Swift's fields were). The field owns it, its text
// color set first.
class SelectionFollowsWindow : public views::ViewObserver,
                               public views::WidgetObserver {
 public:
  explicit SelectionFollowsWindow(views::Textfield* field);
  SelectionFollowsWindow(const SelectionFollowsWindow&) = delete;
  SelectionFollowsWindow& operator=(const SelectionFollowsWindow&) = delete;
  ~SelectionFollowsWindow() override;

  // views::ViewObserver:
  void OnViewAddedToWidget(views::View* view) override;
  void OnViewRemovedFromWidget(views::View* view) override;

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void Follow(views::Widget* widget);

  const raw_ptr<views::Textfield> field_;
  base::ScopedObservation<views::View, views::ViewObserver> view_observation_{
      this};
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

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
  SelectionFollowsWindow selection_{this};
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_FIELD_H_
