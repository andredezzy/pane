// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/resize_handle.h"

#include <utility>

#include "pane/model/app_settings.h"
#include "pane/ui/design/screen_reader.h"
#include "pane/ui/sidebar/resize_cursor.h"
#include "pane/ui/view_ids.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/focus/focus_manager.h"

namespace pane {

ResizeHandle::Callbacks::Callbacks() = default;
ResizeHandle::Callbacks::Callbacks(Callbacks&&) = default;
ResizeHandle::Callbacks& ResizeHandle::Callbacks::operator=(Callbacks&&) =
    default;
ResizeHandle::Callbacks::~Callbacks() = default;

ResizeHandle::ResizeHandle(Callbacks callbacks)
    : views::ResizeArea(this), callbacks_(std::move(callbacks)) {
  SetID(kViewIdSidebarResize);
  screen_reader::SetSplitter(GetViewAccessibility());
  GetViewAccessibility().SetName(u"Sidebar width");
  GetViewAccessibility().SetMinValueForRange(model::kSidebarMinWidth);
  GetViewAccessibility().SetMaxValueForRange(model::kSidebarMaxWidth);
  GetViewAccessibility().AddAction(ax::mojom::Action::kIncrement);
  GetViewAccessibility().AddAction(ax::mojom::Action::kDecrement);
  // A splitter has its value and its steps only focusable, as ARIA's
  // separator (AXNodeData::IsRangeValueSupported): unfocusable, it gave a
  // screen reader no width. It takes the focus in accessibility mode alone;
  // the Swift's handle took no Tab.
  SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  SetWidth(model::kSidebarWidth);
}

ResizeHandle::~ResizeHandle() = default;

void ResizeHandle::SetWidth(double width) {
  if (width == width_) {
    return;
  }
  width_ = width;
  GetViewAccessibility().SetValueForRange(width);
  // Said as it changes while a screen reader is on it, as Views' sliders say
  // theirs (Slider::SetValueInternal): Narrator said the arrows' new widths
  // nothing (W8d).
  NotifyAccessibilityEventDeprecated(ax::mojom::Event::kValueChanged, true);
}

ui::Cursor ResizeHandle::GetCursor(const ui::MouseEvent& event) {
  return ResizeCursor(width_ <= model::kSidebarMinWidth
                          ? ResizeLimit::kNarrowest
                      : width_ >= model::kSidebarMaxWidth ? ResizeLimit::kWidest
                                                          : ResizeLimit::kNone);
}

bool ResizeHandle::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyLeftMouseButton() && event.GetClickCount() == 2) {
    start_.reset();
    callbacks_.reset.Run();
    return true;
  }
  start_ = width_;
  return views::ResizeArea::OnMousePressed(event);
}

void ResizeHandle::OnMouseCaptureLost() {
  // The drag ends where it got to, rather than back where it began.
  if (start_) {
    start_.reset();
    callbacks_.end.Run();
  }
}

bool ResizeHandle::HandleAccessibleAction(const ui::AXActionData& action_data) {
  switch (action_data.action) {
    case ax::mojom::Action::kIncrement:
      Step(kStep);
      return true;
    case ax::mojom::Action::kDecrement:
      Step(-kStep);
      return true;
    // A screen reader's focus, which moves the keys here so the arrows
    // resize: Views gives a view that takes the focus in accessibility mode
    // alone none where the keyboard has no such mode, as on Windows, where
    // Narrator's cursor met the edge and the keys stayed where they were.
    case ax::mojom::Action::kFocus:
      if (views::FocusManager* focus = GetFocusManager()) {
        focus->SetFocusedView(this);
        return true;
      }
      return false;
    default:
      return views::ResizeArea::HandleAccessibleAction(action_data);
  }
}

// The arrows come here before the window's own keys move along a group of
// controls (FocusGroups): while it has the keys, they resize, as a slider's.
bool ResizeHandle::SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) {
  return ArrowStep(event).has_value();
}

bool ResizeHandle::OnKeyPressed(const ui::KeyEvent& event) {
  const std::optional<double> step = ArrowStep(event);
  if (!step) {
    return views::ResizeArea::OnKeyPressed(event);
  }
  Step(*step);
  return true;
}

// static
std::optional<double> ResizeHandle::ArrowStep(const ui::KeyEvent& event) {
  if (event.flags() & (ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN |
                       ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN)) {
    return std::nullopt;
  }
  switch (event.key_code()) {
    case ui::VKEY_RIGHT:
    case ui::VKEY_UP:
      return kStep;
    case ui::VKEY_LEFT:
    case ui::VKEY_DOWN:
      return -kStep;
    default:
      return std::nullopt;
  }
}

void ResizeHandle::OnResize(int resize_amount, bool done_resizing) {
  if (!start_) {
    return;
  }
  callbacks_.resize.Run(model::ClampedSidebarWidth(*start_ + resize_amount));
  if (done_resizing) {
    start_.reset();
    callbacks_.end.Run();
  }
}

void ResizeHandle::Step(double delta) {
  callbacks_.resize.Run(model::ClampedSidebarWidth(width_ + delta));
  callbacks_.end.Run();
}

BEGIN_METADATA(ResizeHandle)
END_METADATA

}  // namespace pane
