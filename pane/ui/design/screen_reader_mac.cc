// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/screen_reader.h"

#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/views/accessibility/view_accessibility.h"

// The Mac's: what VoiceOver has read since ADR 0071, the edge now read as
// vertical.

namespace pane::screen_reader {

void SetPicker(views::ViewAccessibility& picker) {
  picker.SetRole(ax::mojom::Role::kPopUpButton);
}

void SetChoices(views::ViewAccessibility& group) {
  group.SetRole(ax::mojom::Role::kGroup);
}

void SetChoice(views::ViewAccessibility& choice, int position, int count) {}

void SetChosen(views::ViewAccessibility& choice, bool chosen) {
  choice.SetIsSelected(chosen);
}

void KeepChosen(views::ViewAccessibility& choice, bool chosen) {}

void SetSidebarList(views::ViewAccessibility& list) {}

void SetProfileRow(views::ViewAccessibility& row,
                   const std::u16string& name,
                   const std::u16string& value,
                   const std::u16string& summary,
                   bool expanded,
                   bool shown) {
  row.SetName(name);
  row.SetValue(value);
  row.SetIsSelected(shown);
}

void SetTabRow(views::ViewAccessibility& row, bool shown) {
  row.SetIsSelected(shown);
}

void SetPlace(views::ViewAccessibility& row, int position, int count) {}

void SetShowing(views::ViewAccessibility& button, bool showing) {
  button.SetIsSelected(showing);
}

void KeepShowing(views::ViewAccessibility& button, bool showing) {}

void SetOpens(views::ViewAccessibility& button, bool menu, bool open) {
  button.SetIsSelected(open);
}

void SetHeading(views::ViewAccessibility& heading, int level) {
  heading.SetRole(ax::mojom::Role::kHeading);
}

void SetIndicator(views::ViewAccessibility& indicator) {
  indicator.SetRole(ax::mojom::Role::kGroup);
}

void SetPinCount(views::ViewAccessibility& field,
                 views::ViewAccessibility& dots,
                 const std::u16string& count) {
  dots.SetName(count);
}

void SetSplitter(views::ViewAccessibility& edge) {
  edge.SetRole(ax::mojom::Role::kSplitter);
  edge.SetIsVertical(true);
}

}  // namespace pane::screen_reader
