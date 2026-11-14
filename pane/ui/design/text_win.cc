// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/text.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include "base/no_destructor.h"
#include "skia/ext/font_utils.h"
#include "third_party/skia/include/core/SkFontArguments.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkFontStyle.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/platform_font.h"

namespace pane {

namespace {

// The user's calls for Windows' text (2026-11-14, with the test VM's renders
// in front of them; Paper 11.8), each one constant.
//
// Segoe UI has no Medium, which Pane asks for its buttons and a few labels:
//   kRegular: Segoe UI Regular in its place.
//   kSemibold: what DirectWrite matches 500 to, Segoe UI Semibold, which the
//     user found bad.
//   kSegoeUiVariable: Windows 11's own UI font, Segoe UI Variable, at 500,
//     every weight of Pane's from it: chosen. Where it isn't (Windows 10),
//     Segoe UI, its Regular in Medium's place.
enum class MediumOnWindows { kRegular, kSemibold, kSegoeUiVariable };
constexpr MediumOnWindows kMediumOnWindows = MediumOnWindows::kSegoeUiVariable;

// Whether Pane's labels take Windows' ClearType, as Chrome's own Views text
// does there: chosen. A label draws it only on the opaque ground under it
// (SetLabelGround), which it paints under its text; any other stays
// grayscale.
constexpr bool kClearTypeLabels = true;

// Segoe UI Variable's weights (its `wght` axis) and the optical sizes Windows
// gives it: "the optical size will automatically match the requested
// font-size", from 8 to 36 (Microsoft's Typography in Windows 11), Pane's
// sizes being Windows' own units, as Windows' type ramp's are.
constexpr float kLightest = 300;
constexpr float kBoldest = 700;
constexpr float kSmallestOpticalSize = 8;
constexpr float kLargestOpticalSize = 36;

// Segoe UI, from gfx's default font, Windows' message font: what Views draws
// everywhere on Windows. With no Segoe UI Variable, or where the user's
// choice is Regular, Regular takes Medium's place.
gfx::FontList SegoeUi(int size, gfx::Font::Weight weight) {
  if (kMediumOnWindows != MediumOnWindows::kSemibold &&
      weight == gfx::Font::Weight::MEDIUM) {
    weight = gfx::Font::Weight::NORMAL;
  }
  const gfx::FontList base;
  return base.Derive(size - base.GetFontSize(), gfx::Font::NORMAL, weight);
}

// Windows 11's Segoe UI Variable, its Text face, whose axes set every weight
// and optical size; none on Windows 10.
const sk_sp<SkTypeface>& SegoeUiVariable() {
  static const base::NoDestructor<sk_sp<SkTypeface>> face(
      skia::DefaultFontMgr()->legacyMakeTypeface("Segoe UI Variable Text",
                                                 SkFontStyle::Normal()));
  return *face;
}

// Segoe UI Variable at `weight` and the optical size Windows picks for
// `size`, wrapped as gfx's font with no match by name, whose weights are the
// named faces alone.
gfx::FontList SegoeUiVariableAt(int size, gfx::Font::Weight weight) {
  const std::array<SkFontArguments::VariationPosition::Coordinate, 2> axes = {{
      {SkFontArguments::VariationPosition::Coordinate::wght,
       std::clamp(static_cast<float>(weight), kLightest, kBoldest)},
      {SkFontArguments::VariationPosition::Coordinate::opsz,
       std::clamp(static_cast<float>(size), kSmallestOpticalSize,
                  kLargestOpticalSize)},
  }};
  sk_sp<SkTypeface> instance =
      SegoeUiVariable()->makeClone(SkFontArguments().setVariationDesignPosition(
          {axes.data(), static_cast<int>(axes.size())}));
  if (!instance) {
    return SegoeUi(size, weight);
  }
  return gfx::FontList(gfx::Font(gfx::PlatformFont::CreateFromSkTypeface(
      std::move(instance), size, std::nullopt)));
}

}  // namespace

gfx::FontList PlatformSystemFont(int size, gfx::Font::Weight weight) {
  if (kMediumOnWindows == MediumOnWindows::kSegoeUiVariable &&
      SegoeUiVariable()) {
    return SegoeUiVariableAt(size, weight);
  }
  return SegoeUi(size, weight);
}

bool LabelsTakeClearType() {
  return kClearTypeLabels;
}

}  // namespace pane
