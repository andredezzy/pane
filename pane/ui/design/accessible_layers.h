// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_ACCESSIBLE_LAYERS_H_
#define PANE_UI_DESIGN_ACCESSIBLE_LAYERS_H_

#include "base/functional/function_ref.h"

namespace views {
class View;
}

namespace pane {

// What a screen reader reaches of the views stacked in `parent`, the window's
// layers: each child that shows, down to the topmost one `is_modal` names
// that shows (the PIN screen, a sheet, a dialog), and nothing under it, as
// the Swift's .isModal kept VoiceOver on the PIN screen (ADR 0071). Views
// leaves out the rest of a window for a modal window over it alone
// (ViewAXPlatformNodeDelegate::GetChildWidgets), not for a modal view in it,
// and counts a view shown inside a hidden one as shown to its own children
// (ViewAccessibility::UpdateInvisibleByInheritanceRecursive), so VoiceOver
// went under the PIN screen to the sidebar and to Settings, and found a
// control of hidden Settings beside the page. A child left out is a leaf, its
// subtree gone with it, and ignored, so it goes too; what was a leaf or
// ignored before stays so when it comes back. Called again whenever a child
// shows, hides, comes, goes, or moves.
void UpdateAccessibleLayers(
    views::View& parent,
    base::FunctionRef<bool(const views::View&)> is_modal);

// Leaves `view` out of what a screen reader reaches, its subtree with it, as
// a layer under a modal one is (a leaf, and ignored), or brings it back as it
// was. For a view that hides views showing in it, which Views can still give
// a screen reader: the profile sheet's proxy fields, hidden in their Reveal,
// were read with the proxy off, their frames empty (W8c).
void LeaveOutOfAccessibility(views::View& view, bool out);

}  // namespace pane

#endif  // PANE_UI_DESIGN_ACCESSIBLE_LAYERS_H_
