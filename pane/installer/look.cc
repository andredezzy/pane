// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/look.h"

#include <windows.h>

#include "base/win/registry.h"

namespace pane::installer {

namespace {

Color Black(float alpha) {
  return Rgba(0x000000, alpha);
}

Color White(float alpha) {
  return Rgba(0xFFFFFF, alpha);
}

}  // namespace

Color Rgb(uint32_t rgb) {
  return Rgba(rgb, 1);
}

Color Rgba(uint32_t rgb, float alpha) {
  return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
          (rgb & 0xFF) / 255.0f, alpha};
}

Color FromColorRef(COLORREF color) {
  return {GetRValue(color) / 255.0f, GetGValue(color) / 255.0f,
          GetBValue(color) / 255.0f, 1};
}

Color Over(const Color& top, const Color& bottom) {
  const float a = top.a + bottom.a * (1 - top.a);
  if (a <= 0) {
    return {0, 0, 0, 0};
  }
  const auto channel = [&](float t, float b) {
    return (t * top.a + b * bottom.a * (1 - top.a)) / a;
  };
  return {channel(top.r, bottom.r), channel(top.g, bottom.g),
          channel(top.b, bottom.b), a};
}

Color Faded(const Color& color, float alpha) {
  return {color.r, color.g, color.b, color.a * alpha};
}

Color Mix(const Color& a, const Color& b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
          a.a + (b.a - a.a) * t};
}

Appearance CurrentAppearance() {
  Appearance appearance;
  DWORD light = 1;
  base::win::RegKey personalize(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      KEY_QUERY_VALUE);
  if (personalize.Valid() &&
      personalize.ReadValueDW(L"AppsUseLightTheme", &light) == ERROR_SUCCESS) {
    appearance.dark = light == 0;
  }
  HIGHCONTRASTW contrast = {.cbSize = sizeof(contrast)};
  if (::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast,
                              0) &&
      (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
    appearance.contrast = true;
    appearance.window = ::GetSysColor(COLOR_WINDOW);
    appearance.window_text = ::GetSysColor(COLOR_WINDOWTEXT);
    appearance.highlight = ::GetSysColor(COLOR_HIGHLIGHT);
    appearance.highlight_text = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
  }
  BOOL animates = TRUE;
  if (::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animates, 0)) {
    appearance.reduced_motion = !animates;
  }
  return appearance;
}

Palette::Palette(const Appearance& appearance) : contrast(appearance.contrast) {
  if (appearance.contrast) {
    // Paper 11.12.8: Window under WindowText, every edge WindowText, what
    // stands out Highlight under HighlightText, nothing red, no shadow.
    const Color window = FromColorRef(appearance.window);
    const Color text = FromColorRef(appearance.window_text);
    const Color highlight = FromColorRef(appearance.highlight);
    const Color highlight_text = FromColorRef(appearance.highlight_text);
    card = window;
    foreground = message = muted = text;
    track = Faded(window, 0);
    fill = highlight;
    edge = text;
    outline_edge = text;
    outline_hover = Faded(window, 0);
    outline_lit_text = text;
    chip = Faded(window, 0);
    chip_text = text;
    press = Faded(window, 0);
    primary = destructive = highlight;
    primary_text = destructive_text = highlight_text;
    primary_chip = Faded(highlight, 0);
    primary_chip_text = highlight_text;
    press_on_strong = Faded(window, 0);
    ring = highlight;
    box_edge = text;
    box_fill = highlight;
    box_check = highlight_text;
    glyph = text;
    glyph_hover_fill = glyph_press_fill = highlight;
    close_fill = close_press_fill = highlight;
    close_glyph = glyph_lit = highlight_text;
    return;
  }
  const bool dark = appearance.dark;
  card = dark ? Rgb(0x1C1C1F) : Rgb(0xFFFFFF);
  foreground = dark ? Rgb(0xFAFAFA) : Rgb(0x18181B);
  // Pane's dialog message: 72% of the text, 68% on dark.
  message = Over(Faded(foreground, dark ? 0.68f : 0.72f), card);
  muted = dark ? Rgb(0xA1A1AA) : Rgb(0x71717A);
  track = dark ? Rgb(0x3F3F46) : Rgb(0xE4E4E7);
  fill = foreground;
  edge = Faded(card, 0);
  outline_edge = dark ? White(0.1f) : Black(0.08f);
  outline_hover = Faded(foreground, 0.04f);
  outline_lit_text = dark ? Rgb(0xE4E4E7) : Rgb(0x27272A);
  chip = dark ? Rgb(0x232326) : Rgb(0xE9E9EB);
  chip_text = muted;
  press = Faded(foreground, 0.06f);
  primary = foreground;
  primary_text = dark ? Rgb(0x18181B) : Rgb(0xFAFAFA);
  primary_chip = dark ? Rgba(0x18181B, 0.12f) : White(0.18f);
  primary_chip_text = dark ? Rgba(0x18181B, 0.7f) : White(0.85f);
  press_on_strong = Faded(card, 0.1f);
  destructive = dark ? Rgb(0xEF4444) : Rgb(0xDC2626);
  destructive_text = Rgb(0xFAFAFA);
  ring = foreground;
  box_edge = Faded(foreground, 0.3f);
  box_fill = foreground;
  box_check = primary_text;
  // Chrome's WindowsCaptionButton (ADR 0126): the glyph's colour at 10%
  // under the pointer, 20% pressed; Close #E81123 with a white X, pressed
  // at 0x98.
  glyph = dark ? Rgb(0xFFFFFF) : Rgb(0x000000);
  glyph_hover_fill = Faded(glyph, 0.1f);
  glyph_press_fill = Faded(glyph, 0.2f);
  close_fill = Rgb(0xE81123);
  close_press_fill = Rgba(0xE81123, 0x98 / 255.0f);
  close_glyph = Rgb(0xFFFFFF);
  glyph_lit = glyph;
}

}  // namespace pane::installer
