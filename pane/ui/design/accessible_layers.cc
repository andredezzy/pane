// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/accessible_layers.h"

#include <cstddef>

#include "ui/base/class_property.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"

namespace pane {

namespace {

// What leaving a child out changed of it, so that alone is undone.
constexpr int kMadeLeaf = 1 << 0;
constexpr int kMadeIgnored = 1 << 1;
constexpr int kLeftOut = 1 << 2;

DEFINE_UI_CLASS_PROPERTY_KEY(int, kAccessibleLayerKey, 0)

void LeaveOut(views::View& child, bool out) {
  const int was = child.GetProperty(kAccessibleLayerKey);
  if (out == ((was & kLeftOut) != 0)) {
    return;
  }
  views::ViewAccessibility& accessibility = child.GetViewAccessibility();
  if (out) {
    int made = kLeftOut;
    // Its own flag, not the platform's leaf, which an empty view is too.
    if (!accessibility.ViewAccessibility::IsLeaf()) {
      accessibility.SetIsLeaf(true);
      made |= kMadeLeaf;
    }
    if (!accessibility.GetIsIgnored()) {
      accessibility.SetIsIgnored(true);
      made |= kMadeIgnored;
    }
    child.SetProperty(kAccessibleLayerKey, made);
    return;
  }
  if (was & kMadeIgnored) {
    accessibility.SetIsIgnored(false);
  }
  if (was & kMadeLeaf) {
    accessibility.SetIsLeaf(false);
  }
  child.ClearProperty(kAccessibleLayerKey);
}

}  // namespace

void UpdateAccessibleLayers(
    views::View& parent,
    base::FunctionRef<bool(const views::View&)> is_modal) {
  const views::View::Views& children = parent.children();
  size_t floor = 0;
  for (size_t index = children.size(); index-- > 0;) {
    const views::View& child = *children[index];
    if (child.GetVisible() && is_modal(child)) {
      floor = index;
      break;
    }
  }
  for (size_t index = 0; index < children.size(); ++index) {
    views::View& child = *children[index];
    LeaveOut(child, !child.GetVisible() || index < floor);
  }
}

}  // namespace pane
