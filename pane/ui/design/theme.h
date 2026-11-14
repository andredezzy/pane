// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_THEME_H_
#define PANE_UI_DESIGN_THEME_H_

#include <optional>
#include <string_view>

#include "pane/model/app_settings.h"
#include "ui/native_theme/native_theme.h"

namespace views {
class View;
}

namespace pane {

// The Theme (CONTEXT.md): one knob for Pane's chrome, the system's menus and
// dialogs, and every page's prefers-color-scheme (ADR 0001), through
// Chromium's app-level color scheme. Each widget's color provider follows it,
// so every color a view took as a ui::ColorVariant resolves again at once, and
// so does its NSWindow's appearance (from its background's color), which the
// menus it shows take; a page's prefers-color-scheme follows its widget's.
// Switches apply at once, with no cross-fade (DESIGN.md › Theme).
void ApplyTheme(model::Theme theme);

// What the Theme asks of the app-level color scheme: light, dark, or nullopt
// to follow the system's, which a test copy takes from `system_appearance`
// ("light" or "dark") since a scenario can't change the Mac's.
std::optional<ui::NativeTheme::PreferredColorScheme> PreferredColorSchemeFor(
    model::Theme theme,
    std::optional<std::string_view> system_appearance);

// Every view under `root`, `root` included, paints again, hidden ones once
// they show: what a theme's change asks of a window (ADR 0001). A view on a
// layer of its own keeps what it painted until it paints again, so one that
// paints from the theme without repainting at its change kept the theme
// before's colors, as the profile sheet's switch knob and the PIN screen's
// Back and dots did, dark on dark.
void PaintAgain(views::View& root);

}  // namespace pane

#endif  // PANE_UI_DESIGN_THEME_H_
