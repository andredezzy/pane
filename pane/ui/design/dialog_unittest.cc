// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/dialog.h"

#include <memory>
#include <utility>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/size_f.h"
#include "ui/views/view.h"

namespace pane {

namespace {

// A card 10 in from a 1-point ring outside it, its shadow 16 down, a
// Gaussian of 24, around content 40 tall.
std::unique_ptr<DialogCard> MakeCard(bool edge_outside) {
  DialogCard::Look look;
  look.edge_width = 1;
  look.edge_outside = edge_outside;
  look.padding = gfx::Insets(10);
  look.shadow_offset = gfx::Vector2dF(0, 16);
  look.shadow_sigma = 24;
  auto content = std::make_unique<views::View>();
  content->SetPreferredSize(gfx::Size(100, 40));
  return std::make_unique<DialogCard>(look, std::move(content));
}

// Views need an accessibility platform to be made.
class DialogCardTest : public testing::Test {
 private:
  ui::AXPlatformForTest ax_platform_;
};

}  // namespace

// The card is as tall as its content at the width its padding leaves, its
// ring outside it adding to it, or inside it adding nothing; it stands on
// whole points, in a layer as big as its shadow reaches (three deviations,
// moved down), and its content sits inside its ring and padding.
TEST_F(DialogCardTest, SizesAndPlacesTheCardInItsShadowsReach) {
  std::unique_ptr<DialogCard> card = MakeCard(/*edge_outside=*/true);
  EXPECT_EQ(card->SizeFor(120), gfx::SizeF(122, 62));
  EXPECT_EQ(MakeCard(/*edge_outside=*/false)->SizeFor(120),
            gfx::SizeF(120, 60));

  EXPECT_TRUE(card->Place(gfx::RectF(10.5f, 20, 122, 62)));
  EXPECT_EQ(card->CardBounds(), gfx::Rect(10, 20, 122, 62));
  EXPECT_EQ(card->bounds(), gfx::Rect(10 - 72, 20 - 56, 122 + 144, 62 + 144));
  EXPECT_EQ(card->content()->bounds(), gfx::Rect(11, 11, 100, 40));
  // The same place again moves nothing.
  EXPECT_FALSE(card->Place(gfx::RectF(10.5f, 20, 122, 62)));
}

// A press on the card is the card's; one on its shadow goes to what lies
// under it.
TEST_F(DialogCardTest, TakesPressesOnTheCardAlone) {
  std::unique_ptr<DialogCard> card = MakeCard(/*edge_outside=*/true);
  card->Place(gfx::RectF(100, 100, 122, 62));
  const gfx::Rect card_in_layer =
      card->CardBounds() - card->bounds().OffsetFromOrigin();
  EXPECT_TRUE(card->DoesIntersectRect(
      card.get(), gfx::Rect(card_in_layer.CenterPoint(), gfx::Size(1, 1))));
  EXPECT_FALSE(card->DoesIntersectRect(card.get(), gfx::Rect(2, 2, 1, 1)));
  EXPECT_FALSE(card->DoesIntersectRect(
      card.get(),
      gfx::Rect(card_in_layer.x(), card_in_layer.bottom() + 20, 1, 1)));
}

}  // namespace pane
