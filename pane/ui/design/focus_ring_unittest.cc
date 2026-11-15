// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/focus_ring.h"

#include <cmath>
#include <memory>
#include <utility>

#include "base/functional/callback_helpers.h"
#include "base/test/task_environment.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/palette.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"

namespace pane {
namespace {

// Views' buttons ask the accessibility platform for themselves as they are
// made.
class FocusRingTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  ui::AXPlatformForTest ax_platform_;
};

class TestButton : public views::Button {
 public:
  explicit TestButton(PressedCallback pressed)
      : views::Button(std::move(pressed)) {}
};

// How far a ring reaches past the control's edge.
int Reach(float gap, float width) {
  return static_cast<int>(std::ceil(gap + width));
}

}  // namespace

// A control's ring is the system's: on the Mac the Swift's 1-point grey, on
// Windows 2 points in the text's color (Paper 11.17 B); either way it takes
// the system's color id, and is no wider than 2.
TEST_F(FocusRingTest, AControlsRingIsTheSystems) {
  const FocusRingStyle style = ControlFocusRingStyle();
  EXPECT_TRUE(style.color == kPaneColorRing ||
              style.color == kPaneColorKeyboardRing);
  EXPECT_GE(style.width, 1);
  EXPECT_LE(style.width, 2);
  EXPECT_GE(style.off_fill, 0);
}

// The ring goes just outside the control, its line's width larger all round;
// on a strong fill it stands as far off as the system has it, the card
// between (Paper 11.18); a field's is the 1-point ring on every system.
TEST_F(FocusRingTest, StandsOffAStrongFillAsTheSystemHasIt) {
  const FocusRingStyle style = ControlFocusRingStyle();
  views::View parent;

  auto* control = parent.AddChildView(std::make_unique<views::View>());
  FocusRing* ring =
      FocusRing::Install(control, ControlShape::Squircle(radius::kLg));
  control->SetBounds(0, 0, 80, 36);
  const int bare = Reach(0, style.width);
  EXPECT_EQ(ring->bounds(),
            gfx::Rect(-bare, -bare, 80 + 2 * bare, 36 + 2 * bare));
  ring->SetOnFill(true);
  const int off = Reach(style.off_fill, style.width);
  EXPECT_EQ(ring->bounds(), gfx::Rect(-off, -off, 80 + 2 * off, 36 + 2 * off));

  auto* field = parent.AddChildView(std::make_unique<views::View>());
  FocusRing* field_ring = FocusRing::Install(
      field, ControlShape::Squircle(radius::kMd), FocusRing::Kind::kField);
  field_ring->SetOnFill(true);
  field->SetBounds(0, 0, 200, 36);
  EXPECT_EQ(field_ring->bounds(), gfx::Rect(-1, -1, 202, 38));
}

// A dialog's filled buttons stand their ring off their fill, the others not:
// Exit Pane?'s Exit and a page's default are strong fills, a Cancel's
// outline and a ghost are not (Paper 11.18).
TEST_F(FocusRingTest, StrongFillsAreThePrimaryAndTheDestructive) {
  EXPECT_TRUE(ButtonLook(ButtonVariant::kPrimary).strong_fill);
  EXPECT_TRUE(ButtonLook(ButtonVariant::kDestructive).strong_fill);
  EXPECT_TRUE(ButtonLook(ButtonVariant::kPopupPrimary).strong_fill);
  for (const ButtonVariant variant :
       {ButtonVariant::kOutline, ButtonVariant::kSecondary,
        ButtonVariant::kGhost, ButtonVariant::kAlert,
        ButtonVariant::kAlertDefault, ButtonVariant::kPopupCancel}) {
    EXPECT_FALSE(ButtonLook(variant).strong_fill);
  }
}

// A button's highlight installs the system's ring, which a strong fill moves
// off its edge.
TEST_F(FocusRingTest, AButtonsHighlightCarriesTheRing) {
  const FocusRingStyle style = ControlFocusRingStyle();
  views::View parent;
  auto made = std::make_unique<TestButton>(views::Button::PressedCallback());
  TestButton* button = made.get();
  parent.AddChildView(std::unique_ptr<views::View>(std::move(made)));
  ControlHighlight highlight(button, ControlShape::Squircle(radius::kLg), 0.5f,
                             base::DoNothing());
  highlight.ring()->SetOnFill(true);
  button->SetBounds(0, 0, 120, 36);
  const int reach = Reach(style.off_fill, style.width);
  EXPECT_EQ(highlight.ring()->bounds(),
            gfx::Rect(-reach, -reach, 120 + 2 * reach, 36 + 2 * reach));
}

}  // namespace pane
