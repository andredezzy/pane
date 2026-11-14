// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/theme.h"

#include <array>
#include <memory>

#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace pane {

namespace {

// A view that counts the times it was asked to paint again.
class CountedView : public views::View {
  METADATA_HEADER(CountedView, views::View)

 public:
  int asked() const { return asked_; }

 protected:
  void OnDidSchedulePaint(const gfx::Rect& rect) override { ++asked_; }

 private:
  int asked_ = 0;
};

BEGIN_METADATA(CountedView)
END_METADATA

// Views ask the accessibility platform for themselves as they change.
class ThemeTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  ui::AXPlatformForTest ax_platform_;
};

}  // namespace

using Scheme = ui::NativeTheme::PreferredColorScheme;

// Light and dark set the app's scheme whatever the system's; System leaves it
// to the system, or to a test copy's stand-in for it.
TEST_F(ThemeTest, TheThemeSetsTheAppsScheme) {
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kLight, std::nullopt),
            Scheme::kLight);
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kLight, "dark"),
            Scheme::kLight);
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kDark, std::nullopt),
            Scheme::kDark);
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kDark, "light"),
            Scheme::kDark);
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kSystem, std::nullopt),
            std::nullopt);
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kSystem, "light"),
            Scheme::kLight);
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kSystem, "dark"),
            Scheme::kDark);
  // A value no scenario writes is the system's.
  EXPECT_EQ(PreferredColorSchemeFor(model::Theme::kSystem, "sepia"),
            std::nullopt);
}

// Applying a Theme sets Chromium's one app-level knob, which every native
// theme, and so every widget's colors and every page's scheme, follows.
TEST_F(ThemeTest, ApplyingSetsTheOverride) {
  ApplyTheme(model::Theme::kDark);
  EXPECT_EQ(ui::NativeTheme::GetPreferredColorSchemeOverride(), Scheme::kDark);
  ApplyTheme(model::Theme::kLight);
  EXPECT_EQ(ui::NativeTheme::GetPreferredColorSchemeOverride(), Scheme::kLight);
  // No test copy's stand-in in a unit test: the system's.
  ApplyTheme(model::Theme::kSystem);
  EXPECT_EQ(ui::NativeTheme::GetPreferredColorSchemeOverride(), std::nullopt);
}

// A theme's change paints every view again, one on a layer of its own, one
// under a hidden view, and a hidden one included: each keeps what it painted
// otherwise, in the theme before's colors (ADR 0001).
TEST_F(ThemeTest, EveryViewPaintsAgain) {
  CountedView root;
  auto* layered = root.AddChildView(std::make_unique<CountedView>());
  layered->SetPaintToLayer();
  auto* in_layered = layered->AddChildView(std::make_unique<CountedView>());
  auto* hidden = root.AddChildView(std::make_unique<CountedView>());
  hidden->SetVisible(false);
  auto* under_hidden = hidden->AddChildView(std::make_unique<CountedView>());
  const std::array<CountedView*, 5> views = {&root, layered, in_layered, hidden,
                                             under_hidden};
  std::array<int, 5> before;
  for (size_t i = 0; i < views.size(); ++i) {
    before[i] = views[i]->asked();
  }
  PaintAgain(root);
  for (size_t i = 0; i < views.size(); ++i) {
    EXPECT_GT(views[i]->asked(), before[i]) << "view " << i;
  }
}

}  // namespace pane
