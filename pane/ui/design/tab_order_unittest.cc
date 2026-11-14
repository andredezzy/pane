// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/tab_order.h"

#include <memory>
#include <vector>

#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"

namespace pane {
namespace {

class TestButton : public views::Button {
 public:
  TestButton() : views::Button(PressedCallback()) {}
};

// Views' buttons ask the accessibility platform for themselves as they are
// made.
class TabOrderTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  ui::AXPlatformForTest ax_platform_;
};

std::unique_ptr<views::View> MakeButton() {
  return std::make_unique<TestButton>();
}

// A field's stand-in: what isn't a button and takes Tab.
std::unique_ptr<views::View> MakeField() {
  auto field = std::make_unique<views::View>();
  field->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  return field;
}

// The toolbar's and the sidebar's buttons take no Tab on either system:
// Windows' by KeepButtonsOutOfTab, the Mac's by Views' own rule there; a
// field keeps it (ADR 0019).
TEST_F(TabOrderTest, ButtonsTakeNoTabFieldsDo) {
  views::View group;
  views::View* back = group.AddChildView(MakeButton());
  views::View* field = group.AddChildView(MakeField());
  views::View* row = group.AddChildView(std::make_unique<views::View>());
  views::View* in_row = row->AddChildView(MakeButton());
  KeepButtonsOutOfTab(group);
  for (const views::View* button : {back, in_row}) {
    EXPECT_NE(button->GetFocusBehavior(), views::View::FocusBehavior::ALWAYS);
  }
  EXPECT_EQ(field->GetFocusBehavior(), views::View::FocusBehavior::ALWAYS);
}

// The arrows go from button to button in their order on screen, past what
// isn't one, hidden, or disabled, and never into a button's own: a tab's X
// is its row's.
TEST_F(TabOrderTest, ButtonsInOrder) {
  views::View group;
  views::View* first = group.AddChildView(MakeButton());
  group.AddChildView(MakeField());
  views::View* hidden = group.AddChildView(MakeButton());
  hidden->SetVisible(false);
  views::View* disabled = group.AddChildView(MakeButton());
  disabled->SetEnabled(false);
  views::View* list = group.AddChildView(std::make_unique<views::View>());
  views::View* row = list->AddChildView(MakeButton());
  row->AddChildView(MakeButton());
  views::View* last = group.AddChildView(MakeButton());
  EXPECT_EQ(ButtonsInOrder(group),
            (std::vector<views::View*>{first, row, last}));
}

}  // namespace
}  // namespace pane
