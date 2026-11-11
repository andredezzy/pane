// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/checkbox.h"

#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/native_theme/native_theme.h"

namespace pane {

void PaintSystemCheckbox(gfx::Canvas& canvas,
                         const ui::ColorProvider* colors,
                         const gfx::Rect& bounds,
                         const SystemCheckboxState& state) {
  ui::NativeTheme::ButtonExtraParams button;
  button.checked = state.checked;
  const ui::NativeTheme::State theme_state =
      !state.enabled  ? ui::NativeTheme::kDisabled
      : state.pressed ? ui::NativeTheme::kPressed
                      : ui::NativeTheme::kNormal;
  ui::NativeTheme::GetInstanceForNativeUi()->Paint(
      canvas.sk_canvas(), colors, ui::NativeTheme::kCheckbox, theme_state,
      bounds, ui::NativeTheme::ExtraParams(button), /*forced_colors=*/false,
      state.dark ? ui::NativeTheme::PreferredColorScheme::kDark
                 : ui::NativeTheme::PreferredColorScheme::kLight);
}

}  // namespace pane
