// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/checkbox.h"

#include <map>
#include <tuple>

#include "base/no_destructor.h"
#include "pane/ui/design/checkbox_cell_mac.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"

namespace pane {

void PaintSystemCheckbox(gfx::Canvas& canvas,
                         const ui::ColorProvider* colors,
                         const gfx::Rect& bounds,
                         const SystemCheckboxState& state) {
  // Each look at each scale is drawn once.
  using Key = std::tuple<bool, bool, bool, bool, float>;
  static base::NoDestructor<std::map<Key, SkBitmap>> drawn;
  const float scale = canvas.image_scale();
  const Key key(state.checked, state.pressed, state.enabled, state.dark, scale);
  auto found = drawn->find(key);
  if (found == drawn->end()) {
    found = drawn->emplace(key, DrawSystemCheckboxCell(state, scale)).first;
  }
  canvas.DrawImageInt(gfx::ImageSkia::CreateFromBitmap(found->second, scale),
                      bounds.x(), bounds.y());
}

}  // namespace pane
