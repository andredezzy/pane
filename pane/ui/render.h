// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_RENDER_H_
#define PANE_UI_RENDER_H_

#include <cstdint>
#include <optional>
#include <vector>

namespace views {
class View;
}

namespace pane {

// `root` drawn as a PNG at `scale`, as its window shows it but with every
// page left out, as the Swift app's render was (its window's host view,
// cached): what a scenario's render compares with Paper and the Swift UI's.
// Painted in software, each view with a layer of its own drawn where its
// layer puts it, with its opacity.
std::optional<std::vector<uint8_t>> RenderView(views::View* root, float scale);

}  // namespace pane

#endif  // PANE_UI_RENDER_H_
