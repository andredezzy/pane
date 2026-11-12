// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/resize_handle.h"

#include <utility>

#include "pane/model/app_settings.h"
#include "pane/ui/sidebar/resize_cursor.h"
#include "pane/ui/view_ids.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace pane {

ResizeHandle::Callbacks::Callbacks() = default;
ResizeHandle::Callbacks::Callbacks(Callbacks&&) = default;
ResizeHandle::Callbacks& ResizeHandle::Callbacks::operator=(Callbacks&&) =
    default;
ResizeHandle::Callbacks::~Callbacks() = default;

ResizeHandle::ResizeHandle(Callbacks callbacks)
    : views::ResizeArea(this), callbacks_(std::move(callbacks)) {
  SetID(kViewIdSidebarResize);
  GetViewAccessibility().SetRole(ax::mojom::Role::kSplitter);
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
      Step(10);
      return true;
    case ax::mojom::Action::kDecrement:
      Step(-10);
      return true;
    default:
      return views::ResizeArea::HandleAccessibleAction(action_data);
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
