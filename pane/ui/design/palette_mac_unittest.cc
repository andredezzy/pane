// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/palette.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace pane {

// The Mac dims a field's selection while its window isn't key, as its text
// views do: the unemphasized selection, not the key window's Highlight.
TEST(PaletteMacTest, TheSelectionDimsWhileTheWindowIsntKey) {
  for (const bool dark : {false, true}) {
    EXPECT_NE(SystemTextSelection(dark, /*key_window=*/true),
              SystemTextSelection(dark, /*key_window=*/false));
  }
}

}  // namespace pane
