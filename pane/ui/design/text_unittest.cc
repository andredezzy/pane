// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/text.h"

#include <memory>
#include <string>

#include "pane/ui/design/palette.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"

namespace pane {

namespace {

// Views need an accessibility platform to be made.
class DigitCellsLabelTest : public testing::Test {
 private:
  ui::AXPlatformForTest ax_platform_;
};

class ScreenReaderTextTest : public testing::Test {
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

// Text in a view that holds the keys, as the PIN screen's attempts left:
// Chromium leaves it out of what a screen reader reaches, as a button's own
// label, until it is exposed, which takes the focus in accessibility mode
// alone, so Tab passes it by.
TEST_F(ScreenReaderTextTest, TextUnderAViewHoldingTheKeysIsReadOnceExposed) {
  views::View panel;
  panel.SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  const auto add_text = [&panel](const std::u16string& text) {
    views::View* view = panel.AddChildView(std::make_unique<views::View>());
    view->GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);
    view->GetViewAccessibility().SetName(text);
    return view;
  };
  views::View* left_out = add_text(u"Enter at least 4 digits");
  views::View* read = add_text(u"4 attempts remaining");
  ExposeToScreenReaders(*read);

  EXPECT_TRUE(left_out->GetViewAccessibility().GetIsIgnored());
  EXPECT_FALSE(read->GetViewAccessibility().GetIsIgnored());
  EXPECT_EQ(read->GetFocusBehavior(),
            views::View::FocusBehavior::ACCESSIBLE_ONLY);
  EXPECT_FALSE(read->IsFocusable());
}

}  // namespace pane
