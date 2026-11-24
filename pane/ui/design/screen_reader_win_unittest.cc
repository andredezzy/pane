// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/screen_reader.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"

namespace pane {

// The sidebar's edge stays a slider to UI Automation, moving sideways, a
// slider's own orientation, with no state saying otherwise: the Mac's
// vertical splitter is VoiceOver's alone (ADR 0071).
TEST(ScreenReaderWinTest, TheEdgeIsASlider) {
  ui::AXPlatformForTest ax_platform;
  views::View edge;
  screen_reader::SetSplitter(edge.GetViewAccessibility());

  ui::AXNodeData data;
  edge.GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_EQ(data.role, ax::mojom::Role::kSlider);
  EXPECT_FALSE(data.HasState(ax::mojom::State::kVertical));
  EXPECT_FALSE(data.HasState(ax::mojom::State::kHorizontal));
}

}  // namespace pane
