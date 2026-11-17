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

}  // namespace

void LeaveOutOfAccessibility(views::View& view, bool out) {
  const int was = view.GetProperty(kAccessibleLayerKey);
  if (out == ((was & kLeftOut) != 0)) {
    return;
  }
  views::ViewAccessibility& accessibility = view.GetViewAccessibility();
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
    view.SetProperty(kAccessibleLayerKey, made);
    return;
  }
  if (was & kMadeIgnored) {
    accessibility.SetIsIgnored(false);
  }
  if (was & kMadeLeaf) {
    accessibility.SetIsLeaf(false);
  }
  view.ClearProperty(kAccessibleLayerKey);
}

void UpdateAccessibleLayers(
    views::View& parent,
    base::FunctionRef<bool(const views::View&)> is_modal) {
  UpdateAccessibleLayers(parent, is_modal, [](const views::View& child) {
    return child.GetVisible();
  });
}

void UpdateAccessibleLayers(
    views::View& parent,
    base::FunctionRef<bool(const views::View&)> is_modal,
    base::FunctionRef<bool(const views::View&)> shows) {
  const views::View::Views& children = parent.children();
  size_t floor = 0;
  for (size_t index = children.size(); index-- > 0;) {
    const views::View& child = *children[index];
    if (shows(child) && is_modal(child)) {
      floor = index;
      break;
    }
  }
  for (size_t index = 0; index < children.size(); ++index) {
    views::View& child = *children[index];
    LeaveOutOfAccessibility(child, !shows(child) || index < floor);
  }
}

}  // namespace pane
