// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/controls.h"

#include "pane/ui/design/palette.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"

namespace pane {

namespace {

class ControlsTest : public testing::Test {
 protected:
  ControlsTest() {
    ui::ColorProviderKey key;
    key.color_mode = ui::ColorProviderKey::ColorMode::kLight;
    AddPaneColors(&colors_, key);
  }

  // `look` painted in a 64-by-32 control on the window's background, at
  // `hover` and `press`: the pixel at (x, y).
  SkColor Painted(const ControlLook& look,
                  double hover,
                  double press,
                  int x,
                  int y) {
    gfx::Canvas canvas(gfx::Size(64, 32), /*image_scale=*/1,
                       /*is_opaque=*/true);
    canvas.DrawColor(colors_.GetColor(kPaneColorWindowBackground));
    PaintControl(canvas, colors_, look, gfx::RectF(0, 0, 64, 32), hover, press);
    return canvas.GetBitmap().getColor(x, y);
  }

  SkColor Over(SkColor color, SkColor ground) {
    return Mix(ground, SkColorSetA(color, 0xFF), SkColorGetA(color) / 255.0);
  }

  ui::ColorProvider colors_;
};

// Channels apart by at most `off`.
void ExpectNear(SkColor actual, SkColor expected, int off = 1) {
  EXPECT_NEAR(SkColorGetR(actual), SkColorGetR(expected), off);
  EXPECT_NEAR(SkColorGetG(actual), SkColorGetG(expected), off);
  EXPECT_NEAR(SkColorGetB(actual), SkColorGetB(expected), off);
}

}  // namespace

// The primary button: its fill at rest, at 90% under the pointer, and the
// card's 10% over it pressed, as PaneButtonStyle drew it.
TEST_F(ControlsTest, PrimaryLightensUnderThePointerAndDarkensPressed) {
  const ControlLook look = ButtonLook(ButtonVariant::kPrimary);
  const SkColor ground = colors_.GetColor(kPaneColorWindowBackground);
  const SkColor primary = colors_.GetColor(kPaneColorPrimary);
  ExpectNear(Painted(look, 0, 0, 32, 16), primary);
  ExpectNear(Painted(look, 1, 0, 32, 16),
             Over(SkColorSetA(primary, AlphaOf(0.9f)), ground));
  ExpectNear(Painted(look, 1, 1, 32, 16),
             Over(colors_.GetColor(kPaneColorPressOnStrong),
                  Over(SkColorSetA(primary, AlphaOf(0.9f)), ground)));
  // Halfway through the hover's fade, halfway to its 90%.
  ExpectNear(Painted(look, 0.5, 0, 32, 16),
             Over(SkColorSetA(primary, AlphaOf(0.95f)), ground));
}

// The outline button: no fill but the hover's 4%, and its border just inside
// the edge, over whatever it sits on (ADR 0063).
TEST_F(ControlsTest, OutlineIsItsBorderAndItsHover) {
  const ControlLook look = ButtonLook(ButtonVariant::kOutline);
  const SkColor ground = colors_.GetColor(kPaneColorWindowBackground);
  EXPECT_EQ(Painted(look, 0, 0, 32, 16), ground);
  ExpectNear(Painted(look, 1, 0, 32, 16),
             Over(colors_.GetColor(kPaneColorOutlineHover), ground));
  // The border's 1 point on the straight top edge, a squircle's corner clear.
  ExpectNear(Painted(look, 0, 0, 32, 0),
             Over(colors_.GetColor(kPaneColorBorder), ground));
  EXPECT_EQ(Painted(look, 0, 0, 0, 0), ground);
}

// A selected row's fill shows whatever the pointer does; an unselected one
// shows the accent only under it.
TEST_F(ControlsTest, RowsFillOnHoverOrSelection) {
  const SkColor ground = colors_.GetColor(kPaneColorWindowBackground);
  EXPECT_EQ(Painted(RowLook(false), 0, 0, 32, 16), ground);
  ExpectNear(Painted(RowLook(false), 1, 0, 32, 16),
             colors_.GetColor(kPaneColorAccent));
  ExpectNear(Painted(RowLook(true), 0, 0, 32, 16),
             Over(colors_.GetColor(kPaneColorRowSelected), ground));
  ExpectNear(Painted(RowLook(true), 1, 0, 32, 16),
             Over(colors_.GetColor(kPaneColorRowSelected), ground));
}

// A tint turns with the hover, from its color at rest to its lit one; one
// with no lit tint stays.
TEST_F(ControlsTest, TintsTurnWithTheHover) {
  const ControlLook icon = IconButtonLook();
  EXPECT_EQ(TintOf(icon, colors_, 0),
            colors_.GetColor(kPaneColorMutedForeground));
  EXPECT_EQ(TintOf(icon, colors_, 1),
            colors_.GetColor(kPaneColorAccentForeground));
  EXPECT_EQ(TintOf(icon, colors_, 0.5),
            Mix(colors_.GetColor(kPaneColorMutedForeground),
                colors_.GetColor(kPaneColorAccentForeground), 0.5));
  EXPECT_EQ(TintOf(ButtonLook(ButtonVariant::kPrimary), colors_, 1),
            colors_.GetColor(kPaneColorPrimaryForeground));
  EXPECT_EQ(TintOf(RowLook(false, /*tints_on_hover=*/false), colors_, 1),
            colors_.GetColor(kPaneColorMutedForeground));
}

// The looks' disabled opacities are the Swift's: 50% for a button, 40% for
// an icon button, and none of its own for a row.
TEST_F(ControlsTest, DisabledOpacitiesAreTheSwifts) {
  EXPECT_FLOAT_EQ(ButtonLook(ButtonVariant::kGhost).disabled_opacity, 0.5f);
  EXPECT_FLOAT_EQ(IconButtonLook().disabled_opacity, 0.4f);
  EXPECT_FLOAT_EQ(RowLook(false).disabled_opacity, 1);
}

// Circles and capsules stay round; a squircle ring grows its radius with its
// offset.
TEST(ControlShapeTest, ShapesStayTheirKind) {
  SkRect oval;
  EXPECT_TRUE(
      ControlShape::Circle().PathIn(gfx::RectF(0, 0, 40, 32)).isOval(&oval));
  EXPECT_EQ(oval, SkRect::MakeXYWH(4, 0, 32, 32));
  SkRRect capsule;
  EXPECT_TRUE(ControlShape::Capsule()
                  .PathIn(gfx::RectF(0, 0, 36, 20))
                  .isRRect(&capsule));
  EXPECT_EQ(capsule.getSimpleRadii(), SkVector::Make(10, 10));
  EXPECT_FLOAT_EQ(ControlShape::Squircle(11).Outset(1).radius(), 12);
  EXPECT_TRUE(ControlShape::Squircle(11).Outset(1).is_squircle());
  EXPECT_FALSE(ControlShape::Capsule().Outset(1).is_squircle());
}

}  // namespace pane
