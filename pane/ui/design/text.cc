// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/text.h"

#include <map>
#include <utility>

#include "base/no_destructor.h"
#include "ui/gfx/font_list.h"
#include "ui/views/controls/label.h"

namespace pane {

const gfx::FontList& SystemFont(int size, gfx::Font::Weight weight) {
  static base::NoDestructor<
      std::map<std::pair<int, gfx::Font::Weight>, gfx::FontList>>
      fonts;
  const std::pair key(size, weight);
  auto found = fonts->find(key);
  if (found == fonts->end()) {
    // From the platform's UI font, so the Mac's stays the system font (its
    // optical sizes and weights), at the size asked for wherever its default
    // size differs.
    const gfx::FontList base;
    found = fonts
                ->emplace(key, base.Derive(size - base.GetFontSize(),
                                           gfx::Font::NORMAL, weight))
                .first;
  }
  return found->second;
}

void StyleLabel(views::Label& label,
                const TextStyle& style,
                ui::ColorVariant color) {
  label.SetFontList(SystemFont(style.size, style.weight));
  label.SetLineHeight(style.line_height);
  label.SetAutoColorReadabilityEnabled(false);
  label.SetEnabledColor(std::move(color));
  label.SetSubpixelRenderingEnabled(false);
}

std::unique_ptr<views::Label> MakeLabel(const std::u16string& text,
                                        const TextStyle& style,
                                        ui::ColorVariant color) {
  auto label = std::make_unique<views::Label>(text);
  StyleLabel(*label, style, std::move(color));
  return label;
}

}  // namespace pane
