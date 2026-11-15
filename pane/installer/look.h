// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_LOOK_H_
#define PANE_INSTALLER_LOOK_H_

#include <windows.h>

#include <stdint.h>

namespace pane::installer {

// A colour, straight alpha, 0 to 1.
struct Color {
  friend bool operator==(const Color&, const Color&) = default;

  float r = 0;
  float g = 0;
  float b = 0;
  float a = 1;
};

Color Rgb(uint32_t rgb);
Color Rgba(uint32_t rgb, float alpha);
Color FromColorRef(COLORREF color);
// `top` over `bottom`, opaque where `bottom` is.
Color Over(const Color& top, const Color& bottom);
// `color` at `alpha` of its own.
Color Faded(const Color& color, float alpha);
// From `a` to `b`, `t` of the way.
Color Mix(const Color& a, const Color& b, float t);

// Windows' appearance, which the window follows: its apps' light or dark,
// and a contrast theme over either.
struct Appearance {
  friend bool operator==(const Appearance&, const Appearance&) = default;

  bool dark = false;
  bool contrast = false;
  // A contrast theme's roles (GetSysColor), where one is on.
  COLORREF window = 0;
  COLORREF window_text = 0;
  COLORREF highlight = 0;
  COLORREF highlight_text = 0;
  // Windows' animations off (Settings › Accessibility › Visual effects):
  // every fade at once, as Pane's motion does then (ADR 0070).
  bool reduced_motion = false;
};

// As Windows has it now.
Appearance CurrentAppearance();

// Pane's colours for the window, from its tokens (Paper's, Pane's palette):
// the card, the text, the bar, the keyed buttons, the box, the caption
// buttons, light, dark, or a contrast theme's roles, each colour taking one
// as palette_win.cc's AddForcedColors maps Pane's own.
struct Palette {
  explicit Palette(const Appearance& appearance);

  bool contrast;
  Color card;
  Color foreground;
  Color message;
  Color muted;
  // The bar's track (none under a contrast theme, which edges it) and fill.
  Color track;
  Color fill;
  // Under a contrast theme: WindowText, every edge's.
  Color edge;

  // The outlined keyed button: its edge, its hover over the card, the text
  // it takes under the pointer, its key chip and the chip's text.
  Color outline_edge;
  Color outline_hover;
  Color outline_lit_text;
  Color chip;
  Color chip_text;
  // A press over any button.
  Color press;
  // The filled default and its chip.
  Color primary;
  Color primary_text;
  Color primary_chip;
  Color primary_chip_text;
  // What a press puts over a strong fill.
  Color press_on_strong;
  Color destructive;
  Color destructive_text;
  // The keyboard's ring (11.17 B).
  Color ring;
  // The box: its edge, ticked its fill and its check.
  Color box_edge;
  Color box_fill;
  Color box_check;

  // The caption buttons' glyphs, their hover and press, and Close's.
  Color glyph;
  Color glyph_hover_fill;
  Color glyph_press_fill;
  Color close_fill;
  Color close_press_fill;
  Color close_glyph;
  Color glyph_lit;
};

}  // namespace pane::installer

#endif  // PANE_INSTALLER_LOOK_H_
