// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/focus_groups.h"

#include <algorithm>
#include <array>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/notreached.h"
#include "pane/ui/design/tab_order.h"
#include "pane/ui/sidebar/sidebar_view.h"
#include "pane/ui/toolbar/address_field.h"
#include "pane/ui/toolbar/toolbar_view.h"
#include "pane/ui/window/root_view.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"

namespace pane {

namespace {

// The groups F6 moves the keys along, in its order (Chrome's: the address
// first from the page).
enum class Group { kAddress, kToolbar, kSidebar, kPage };
constexpr std::array<Group, 4> kGroups = {Group::kAddress, Group::kToolbar,
                                          Group::kSidebar, Group::kPage};

ui::Accelerator NextKey() {
  return ui::Accelerator(ui::VKEY_F6, ui::EF_NONE);
}
ui::Accelerator PreviousKey() {
  return ui::Accelerator(ui::VKEY_F6, ui::EF_SHIFT_DOWN);
}
ui::Accelerator ToolbarKey() {
  return ui::Accelerator(ui::VKEY_T, ui::EF_ALT_DOWN | ui::EF_SHIFT_DOWN);
}

// Within a group of buttons: the arrows, Home, End, and Esc.
constexpr std::array<ui::KeyboardCode, 7> kWithinGroup = {
    ui::VKEY_LEFT, ui::VKEY_RIGHT, ui::VKEY_UP,    ui::VKEY_DOWN,
    ui::VKEY_HOME, ui::VKEY_END,   ui::VKEY_ESCAPE};

class FocusGroupsWin : public FocusGroups,
                       public ui::AcceleratorTarget,
                       public views::FocusChangeListener {
 public:
  explicit FocusGroupsWin(RootView& root) : root_(root) {}
  ~FocusGroupsWin() override { Leave(); }

  // FocusGroups:
  bool Move(const ui::Accelerator& key) override {
    focus_ = root_->GetFocusManager();
    if (!focus_) {
      return false;
    }
    if (key == ToolbarKey()) {
      return Enter(Group::kToolbar);
    }
    const std::optional<Group> now = Current();
    // In a sheet or a dialog: the keys stay in it, as its own Tab's do.
    if (!now) {
      return false;
    }
    const size_t at =
        static_cast<size_t>(std::ranges::find(kGroups, *now) - kGroups.begin());
    const bool back = key == PreviousKey();
    for (size_t step = 1; step < kGroups.size(); ++step) {
      const size_t next =
          (at + (back ? kGroups.size() - step : step)) % kGroups.size();
      if (Enter(kGroups[next])) {
        return true;
      }
    }
    return false;
  }

  void Detach() override {
    Leave();
    focus_ = nullptr;
  }

  // ui::AcceleratorTarget, while a group's buttons have the keys:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    // None while the group isn't on screen, as when a page fills it.
    if (!group_ || !focus_ || !group_->IsDrawn()) {
      return false;
    }
    if (accelerator.key_code() == ui::VKEY_ESCAPE) {
      GiveBack();
      return true;
    }
    const std::vector<views::View*> buttons = ButtonsInOrder(*group_);
    if (buttons.empty()) {
      return false;
    }
    const views::View* focused = focus_->GetFocusedView();
    const auto found =
        std::ranges::find_if(buttons, [focused](views::View* button) {
          return focused && button->Contains(focused);
        });
    const size_t at = found == buttons.end()
                          ? 0
                          : static_cast<size_t>(found - buttons.begin());
    size_t next = at;
    switch (accelerator.key_code()) {
      case ui::VKEY_LEFT:
      case ui::VKEY_UP:
        next = (at + buttons.size() - 1) % buttons.size();
        break;
      case ui::VKEY_RIGHT:
      case ui::VKEY_DOWN:
        next = (at + 1) % buttons.size();
        break;
      case ui::VKEY_HOME:
        next = 0;
        break;
      case ui::VKEY_END:
        next = buttons.size() - 1;
        break;
      default:
        return false;
    }
    FocusButton(buttons[next]);
    return true;
  }

  bool CanHandleAccelerators() const override { return group_ != nullptr; }

  // views::FocusChangeListener:
  void OnWillChangeFocus(views::View* focused_before,
                         views::View* focused_now) override {}

  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override {
    // The keys left the group: its arrows go with them.
    if (group_ && (!focused_now || !group_->Contains(focused_now))) {
      Leave();
    }
  }

 private:
  // The group that has the keys; the page's while none does.
  std::optional<Group> Current() const {
    const views::View* focused = focus_->GetFocusedView();
    ToolbarView* toolbar = root_->toolbar();
    if (focused && toolbar->address_field()->Contains(focused)) {
      return Group::kAddress;
    }
    if (focused && toolbar->Contains(focused)) {
      return Group::kToolbar;
    }
    if (focused && root_->sidebar()->Contains(focused)) {
      return Group::kSidebar;
    }
    if (!focused || focused == &*root_ || root_->InContent(focused)) {
      return Group::kPage;
    }
    return std::nullopt;
  }

  // Gives `group` the keys, as far as it can take them now.
  bool Enter(Group group) {
    ToolbarView* toolbar = root_->toolbar();
    switch (group) {
      case Group::kAddress:
        if (!toolbar->IsDrawn() || !toolbar->address_field()->IsDrawn()) {
          return false;
        }
        Leave();
        root_->FocusAddress();
        return toolbar->address_field()->HasFocus();
      case Group::kToolbar:
        return toolbar->IsDrawn() && EnterButtons(*toolbar);
      case Group::kSidebar:
        return root_->sidebar()->IsDrawn() && EnterButtons(*root_->sidebar());
      case Group::kPage:
        Leave();
        return root_->FocusContent();
    }
    NOTREACHED();
  }

  // The group's first button takes the keys, and its arrows with them.
  bool EnterButtons(views::View& group) {
    const std::vector<views::View*> buttons = ButtonsInOrder(group);
    if (buttons.empty()) {
      return false;
    }
    views::View* focused = focus_->GetFocusedView();
    if (!group_ || !group_->Contains(focused)) {
      before_.SetView(focused);
    }
    Leave();
    group_ = &group;
    for (const ui::KeyboardCode key : kWithinGroup) {
      focus_->RegisterAccelerator(ui::Accelerator(key, ui::EF_NONE),
                                  ui::AcceleratorManager::kNormalPriority,
                                  this);
    }
    focus_->AddFocusChangeListener(this);
    FocusButton(buttons.front());
    return true;
  }

  void FocusButton(views::View* button) {
    focus_->SetFocusedViewWithReason(
        button, views::FocusManager::FocusChangeReason::kFocusTraversal);
  }

  // Esc: the keys back to what had them before the group, else the page.
  void GiveBack() {
    views::View* before = before_.view();
    before_.SetView(nullptr);
    Leave();
    if (before && before->IsDrawn() &&
        before->GetFocusManager() == focus_.get()) {
      focus_->SetFocusedViewWithReason(
          before, views::FocusManager::FocusChangeReason::kFocusRestore);
      return;
    }
    root_->FocusContent();
  }

  void Leave() {
    if (!group_) {
      return;
    }
    group_ = nullptr;
    if (!focus_) {
      return;
    }
    focus_->RemoveFocusChangeListener(this);
    for (const ui::KeyboardCode key : kWithinGroup) {
      focus_->UnregisterAccelerator(ui::Accelerator(key, ui::EF_NONE), this);
    }
  }

  const raw_ref<RootView> root_;
  raw_ptr<views::FocusManager> focus_ = nullptr;
  // The toolbar or the sidebar, while its buttons have the keys.
  raw_ptr<views::View> group_ = nullptr;
  views::ViewTracker before_;
};

}  // namespace

// static
std::unique_ptr<FocusGroups> FocusGroups::Make(RootView& root) {
  return std::make_unique<FocusGroupsWin>(root);
}

// static
std::vector<ui::Accelerator> FocusGroups::Keys() {
  return {NextKey(), PreviousKey(), ToolbarKey()};
}

}  // namespace pane
