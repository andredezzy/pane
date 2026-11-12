// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/accessible_layers.h"

#include <memory>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"

namespace pane {

namespace {

// Text a screen reader reads, as it reads a label.
std::unique_ptr<views::View> Text(const std::u16string& text) {
  auto view = std::make_unique<views::View>();
  view->GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);
  view->GetViewAccessibility().SetName(text);
  return view;
}

// Views need an accessibility platform to be made.
class AccessibleLayersTest : public testing::Test {
 protected:
  // A layer of the window with a text in it, which a screen reader reaches
  // while the layer is in.
  views::View* AddLayer(views::View& window, const std::u16string& text) {
    views::View* layer = window.AddChildView(std::make_unique<views::View>());
    return layer->AddChildView(Text(text));
  }

  static bool Reached(const views::View& view) {
    return !view.GetViewAccessibility().GetIsIgnored();
  }

 private:
  ui::AXPlatformForTest ax_platform_;
};

}  // namespace

// The PIN screen over the window: what is under it goes, as long as it
// shows, and comes back as it was; what shows over it stays.
TEST_F(AccessibleLayersTest, AModalLayerLeavesOutWhatIsUnderIt) {
  views::View window;
  views::View* sidebar = AddLayer(window, u"Settings");
  views::View* page = AddLayer(window, u"Web content");
  views::View* lock = AddLayer(window, u"4 attempts remaining");
  views::View* hint = AddLayer(window, u"Press Esc to exit full screen");
  views::View* lock_layer = lock->parent();
  lock_layer->SetVisible(false);
  const auto is_modal = [&](const views::View& child) {
    return &child == lock_layer;
  };

  UpdateAccessibleLayers(window, is_modal);
  EXPECT_TRUE(Reached(*sidebar));
  EXPECT_TRUE(Reached(*page));
  EXPECT_FALSE(Reached(*lock));
  EXPECT_TRUE(Reached(*hint));

  lock_layer->SetVisible(true);
  UpdateAccessibleLayers(window, is_modal);
  EXPECT_FALSE(Reached(*sidebar));
  EXPECT_FALSE(Reached(*page));
  EXPECT_TRUE(
      sidebar->parent()->GetViewAccessibility().ViewAccessibility::IsLeaf());
  EXPECT_TRUE(Reached(*lock));
  EXPECT_TRUE(Reached(*hint));

  lock_layer->SetVisible(false);
  UpdateAccessibleLayers(window, is_modal);
  EXPECT_TRUE(Reached(*sidebar));
  EXPECT_TRUE(Reached(*page));
  EXPECT_FALSE(
      sidebar->parent()->GetViewAccessibility().ViewAccessibility::IsLeaf());
  EXPECT_FALSE(Reached(*lock));
}

// A hidden panel stays out whole, a row shown in it while it is hidden
// included (Settings' Lock after, which VoiceOver found beside the page).
TEST_F(AccessibleLayersTest, AHiddenLayerStaysOutWithWhatShowsInIt) {
  views::View window;
  views::View* settings = window.AddChildView(std::make_unique<views::View>());
  views::View* row = settings->AddChildView(std::make_unique<views::View>());
  views::View* lock_after = row->AddChildView(Text(u"Lock after"));
  row->SetVisible(false);
  settings->SetVisible(false);
  const auto none = [](const views::View&) { return false; };
  UpdateAccessibleLayers(window, none);

  row->SetVisible(true);
  UpdateAccessibleLayers(window, none);
  EXPECT_FALSE(Reached(*lock_after));

  settings->SetVisible(true);
  UpdateAccessibleLayers(window, none);
  EXPECT_TRUE(Reached(*lock_after));
}

// A layer ignored by its own design (a shield over the page) is ignored
// again once the modal layer goes, and not a leaf it never was.
TEST_F(AccessibleLayersTest, WhatWasIgnoredBeforeStaysSo) {
  views::View window;
  views::View* shield = window.AddChildView(std::make_unique<views::View>());
  views::View* under = shield->AddChildView(Text(u"Under"));
  shield->GetViewAccessibility().SetIsIgnored(true);
  views::View* dialog = window.AddChildView(std::make_unique<views::View>());
  const auto is_modal = [&](const views::View& child) {
    return &child == dialog;
  };

  UpdateAccessibleLayers(window, is_modal);
  EXPECT_FALSE(Reached(*under));

  dialog->SetVisible(false);
  UpdateAccessibleLayers(window, is_modal);
  EXPECT_TRUE(shield->GetViewAccessibility().GetIsIgnored());
  EXPECT_FALSE(shield->GetViewAccessibility().ViewAccessibility::IsLeaf());
  EXPECT_TRUE(Reached(*under));
}

}  // namespace pane
