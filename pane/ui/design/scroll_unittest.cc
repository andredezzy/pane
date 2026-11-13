// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/scroll.h"

#include <memory>

#include "base/test/scoped_feature_list.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/base/ui_base_features.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/view.h"

namespace pane {

// The profile sheet's body on Windows, where Chromium scrolls without
// layers: rows that take the width they're given, asking for little, taller
// than what shows of them. They lay out as wide as the body, as on the Mac.
TEST(LayeredScrollViewTest, ContentsAreAsWideAsTheViewportWithoutTheFeature) {
  base::test::ScopedFeatureList features;
  features.InitAndDisableFeature(features::kUiCompositorScrollWithLayers);
  // Views need an accessibility platform to be made.
  ui::AXPlatformForTest ax_platform;

  std::unique_ptr<views::ScrollView> scroll = LayeredScrollView();
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  // No scroll bar beside them, on either OS, so the viewport is the body.
  scroll->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll->SetUseContentsPreferredSize(true);
  views::View* rows = scroll->SetContents(std::make_unique<views::View>());
  rows->SetPreferredSize(gfx::Size(10, 600));

  scroll->SetBounds(0, 0, 400, 300);
  scroll->DeprecatedLayoutImmediately();

  EXPECT_EQ(rows->size(), gfx::Size(400, 600));
}

}  // namespace pane
