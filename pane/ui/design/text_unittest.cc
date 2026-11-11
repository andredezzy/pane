// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/text.h"

#include "pane/ui/design/palette.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"

namespace pane {

namespace {

// Views need an accessibility platform to be made.
class DigitCellsLabelTest : public testing::Test {
 private:
  ui::AXPlatformForTest ax_platform_;
};

int WidthOf(DigitCellsLabel& label, const std::u16string& text) {
  label.SetText(text);
  return label.GetPreferredSize().width();
}

}  // namespace

// The find bar's count holds still as it counts: each digit takes a
// figure's cell, so counts of as many digits are as wide, whichever digits
// they are (Paper 10.6); a digit more widens it by one cell.
TEST_F(DigitCellsLabelTest, CountsOfAsManyDigitsAreAsWide) {
  DigitCellsLabel label(TextStyle{12, weight::kRegular, 0},
                        kPaneColorMutedForeground);
  const int one = WidthOf(label, u"1/5");
  EXPECT_EQ(WidthOf(label, u"4/5"), one);
  EXPECT_EQ(WidthOf(label, u"9/5"), one);
  const int ten = WidthOf(label, u"10/12");
  EXPECT_EQ(WidthOf(label, u"11/12"), ten);
  EXPECT_EQ(WidthOf(label, u"88/12"), ten);
  EXPECT_GT(ten, WidthOf(label, u"9/12"));
  EXPECT_EQ(WidthOf(label, u""), 0);
  EXPECT_GT(label.GetPreferredSize().height(), 0);
}

}  // namespace pane
