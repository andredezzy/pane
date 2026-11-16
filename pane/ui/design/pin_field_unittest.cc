// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/pin_field.h"

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/base/ime/text_edit_commands.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/render_text.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/layout/layout_provider.h"

namespace pane {
namespace {

// A text field asks the accessibility platform, the shared resources (its
// words, which read empty here, none being loaded), and the layout's provider
// for itself as it is made.
class PinFieldTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  ui::AXPlatformForTest ax_platform_;
  ui::ResourceBundle resource_bundle_{nullptr};
  ui::ResourceBundle::SharedInstanceSwapperForTesting resources_{
      &resource_bundle_};
  views::LayoutProvider layout_provider_;
};

ui::AXNodeData DataOf(views::View& view) {
  ui::AXNodeData data;
  view.GetViewAccessibility().GetAccessibleNodeData(&data);
  return data;
}

ui::KeyEvent Key(ui::EventType type, ui::KeyboardCode code) {
  return ui::KeyEvent(type, code, ui::EF_NONE);
}

}  // namespace

// A screen reader reads the PIN's field as a password field named PIN, its
// value a bullet per digit typed, never a digit (ADR 0071). It offers no
// menu, which the menu key, Shift+F10, and a screen reader's Show menu would
// open over the Lock, and edits nothing of what it holds.
TEST_F(PinFieldTest, ReadsAsAPasswordFieldAndOffersNothing) {
  PinField field(base::BindRepeating([](const ui::KeyEvent&) { return true; }),
                 base::BindRepeating([] { return true; }));
  field.SetDigits(3);

  const ui::AXNodeData data = DataOf(field);
  EXPECT_EQ(data.role, ax::mojom::Role::kTextField);
  EXPECT_TRUE(data.HasState(ax::mojom::State::kProtected));
  EXPECT_EQ(data.GetString16Attribute(ax::mojom::StringAttribute::kName),
            u"PIN");
  EXPECT_EQ(data.GetString16Attribute(ax::mojom::StringAttribute::kValue),
            std::u16string(3, gfx::RenderText::kPasswordReplacementChar));
  EXPECT_FALSE(data.HasAction(ax::mojom::Action::kShowContextMenu));
  EXPECT_EQ(field.context_menu_controller(), nullptr);

  field.InsertText(
      u"12",
      ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  EXPECT_EQ(field.GetText(), u"000");
  for (const ui::TextEditCommand command :
       {ui::TextEditCommand::PASTE, ui::TextEditCommand::CUT,
        ui::TextEditCommand::DELETE_BACKWARD, ui::TextEditCommand::UNDO}) {
    EXPECT_FALSE(field.IsTextEditCommandEnabled(command));
  }

  field.SetDigits(0);
  EXPECT_EQ(
      DataOf(field).GetString16Attribute(ax::mojom::StringAttribute::kValue),
      u"");
}

// Each key pressed goes to the PIN screen ahead of the field, which takes
// every key while the screen holds them, and none of its own once it
// doesn't.
TEST_F(PinFieldTest, HandsItsKeysToThePinScreen) {
  std::vector<ui::KeyboardCode> pressed;
  bool holds = true;
  PinField field(base::BindLambdaForTesting([&](const ui::KeyEvent& event) {
                   pressed.push_back(event.key_code());
                   return holds;
                 }),
                 base::BindLambdaForTesting([&] { return holds; }));

  const ui::KeyEvent five = Key(ui::EventType::kKeyPressed, ui::VKEY_5);
  EXPECT_TRUE(field.SkipDefaultKeyEventProcessing(five));
  EXPECT_TRUE(field.HandleKeyEvent(&field, five));
  EXPECT_TRUE(field.HandleKeyEvent(
      &field, Key(ui::EventType::kKeyReleased, ui::VKEY_5)));
  EXPECT_EQ(pressed, std::vector<ui::KeyboardCode>{ui::VKEY_5});
  EXPECT_EQ(field.GetText(), u"");

  holds = false;
  EXPECT_FALSE(field.SkipDefaultKeyEventProcessing(five));
  EXPECT_FALSE(field.HandleKeyEvent(&field, five));
  EXPECT_FALSE(field.HandleKeyEvent(
      &field, Key(ui::EventType::kKeyReleased, ui::VKEY_5)));
}

}  // namespace pane
