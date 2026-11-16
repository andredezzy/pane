// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/screen_reader.h"

#include "base/strings/strcat.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/views/accessibility/view_accessibility.h"

// Windows': what Narrator reads right through UI Automation, as it reads
// ARIA's same roles (W8c).

namespace pane::screen_reader {

void SetPicker(views::ViewAccessibility& picker) {
  picker.SetRole(ax::mojom::Role::kComboBoxSelect);
}

void SetChoices(views::ViewAccessibility& group) {
  group.SetRole(ax::mojom::Role::kRadioGroup);
}

void SetChoice(views::ViewAccessibility& choice, int position, int count) {
  choice.SetRole(ax::mojom::Role::kRadioButton);
  choice.SetPosInSet(position);
  choice.SetSetSize(count);
  // Unchosen until SetChosen says otherwise: a radio button with no checked
  // state has no selection in UI Automation, and Narrator said no
  // "non-selected" of the colors not chosen (W8c).
  choice.SetCheckedState(ax::mojom::CheckedState::kFalse);
}

void SetChosen(views::ViewAccessibility& choice, bool chosen) {
  choice.SetCheckedState(chosen ? ax::mojom::CheckedState::kTrue
                                : ax::mojom::CheckedState::kFalse);
}

void KeepChosen(views::ViewAccessibility& choice, bool chosen) {
  SetChosen(choice, chosen);
}

void SetSidebarList(views::ViewAccessibility& list) {
  list.SetRole(ax::mojom::Role::kTree);
}

void SetProfileRow(views::ViewAccessibility& row,
                   const std::u16string& name,
                   const std::u16string& value,
                   const std::u16string& summary,
                   bool expanded,
                   bool shown) {
  row.SetRole(ax::mojom::Role::kTreeItem);
  row.SetName(base::StrCat({name, u", ", summary}));
  row.SetHierarchicalLevel(1);
  if (expanded) {
    row.SetIsExpanded();
  } else {
    row.SetIsCollapsed();
  }
  row.SetIsSelected(shown);
}

void SetTabRow(views::ViewAccessibility& row, bool shown) {
  row.SetRole(ax::mojom::Role::kTreeItem);
  row.SetHierarchicalLevel(2);
  row.SetIsSelected(shown);
}

void SetPlace(views::ViewAccessibility& row, int position, int count) {
  row.SetPosInSet(position);
  row.SetSetSize(count);
}

void SetShowing(views::ViewAccessibility& button, bool showing) {
  button.SetRole(ax::mojom::Role::kToggleButton);
  button.SetCheckedState(showing ? ax::mojom::CheckedState::kTrue
                                 : ax::mojom::CheckedState::kFalse);
}

void KeepShowing(views::ViewAccessibility& button, bool showing) {
  SetShowing(button, showing);
}

void SetOpens(views::ViewAccessibility& button, bool menu, bool open) {
  if (menu) {
    button.SetHasPopup(ax::mojom::HasPopup::kMenu);
  }
  if (open) {
    button.SetIsExpanded();
  } else if (menu) {
    button.SetIsCollapsed();
  } else {
    button.RemoveExpandCollapseState();
  }
}

void SetHeading(views::ViewAccessibility& heading, int level) {
  heading.SetRole(ax::mojom::Role::kHeading);
  heading.SetHierarchicalLevel(level);
}

void SetIndicator(views::ViewAccessibility& indicator) {
  indicator.SetRole(ax::mojom::Role::kImage);
}

void SetPinCount(views::ViewAccessibility& field,
                 views::ViewAccessibility& dots,
                 const std::u16string& count) {
  field.SetDescription(count);
  dots.SetIsIgnored(true);
}

void SetSplitter(views::ViewAccessibility& edge) {
  edge.SetRole(ax::mojom::Role::kSlider);
}

}  // namespace pane::screen_reader
