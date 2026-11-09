// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/window.h"

#include <utility>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/window/frame.h"
#include "pane/ui/window/root_view.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/base/hit_test.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The keys the window takes before the page sees them: a page can't keep a
// user from a new tab or the address field (ADR 0019).
bool IsAppFirst(const ui::Accelerator& accelerator) {
  return accelerator.modifiers() == ui::EF_COMMAND_DOWN &&
         (accelerator.key_code() == ui::VKEY_T ||
          accelerator.key_code() == ui::VKEY_L);
}

}  // namespace

Window::Window(const gfx::Rect& bounds, Controller* controller)
    : controller_(controller) {
  auto root = std::make_unique<RootView>(
      base::BindRepeating(&Window::RowPressed, base::Unretained(this)),
      base::BindRepeating(&Window::AddressEntered, base::Unretained(this)));
  root_ = root.get();
  host_ = std::make_unique<WindowHost>(this, bounds, std::move(root));
  // The window's own fill, from the palette: each Theme change resolves it
  // again, and the Mac's window takes its appearance from it, so the lights
  // and the menus the window shows follow the Theme too.
  host_->widget()->SetBackgroundColor(kPaneColorWindowBackground);

  // The one command table: the window's FocusManager's accelerators.
  views::FocusManager* focus = host_->widget()->GetFocusManager();
  std::vector<ui::Accelerator> accelerators = {
      ui::Accelerator(ui::VKEY_T, ui::EF_COMMAND_DOWN),
      ui::Accelerator(ui::VKEY_L, ui::EF_COMMAND_DOWN)};
  for (int number = 1; number <= 9; ++number) {
    accelerators.emplace_back(
        static_cast<ui::KeyboardCode>(ui::VKEY_0 + number),
        ui::EF_COMMAND_DOWN);
  }
  for (const ui::Accelerator& accelerator : accelerators) {
    focus->RegisterAccelerator(accelerator,
                               ui::AcceleratorManager::kNormalPriority, this);
  }
}

Window::~Window() {
  if (views::Widget* widget = host_ ? host_->widget() : nullptr) {
    widget->GetFocusManager()->UnregisterAccelerators(this);
  }
  // The root view goes with the widget, which the host closes.
  root_ = nullptr;
  host_.reset();
}

void Window::Show(bool activate) {
  if (activate) {
    host_->widget()->Show();
  } else {
    host_->widget()->ShowInactive();
  }
  Refresh();
}

ui::Compositor* Window::compositor() const {
  return host_ && host_->widget() ? host_->widget()->GetCompositor() : nullptr;
}

void Window::ShowTab(content::WebContents* contents, bool chosen) {
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents);
  if (!browser) {
    return;
  }
  host_->SetActiveProfile(browser->GetProfile());
  TabStripModel* model = browser->GetTabStripModel();
  const int index = model->GetIndexOfWebContents(contents);
  if (index != TabStripModel::kNoTab && index != model->active_index()) {
    model->ActivateTabAt(index);
  }
  root_->ShowPage(contents);
  if (chosen) {
    root_->SetAddress(base::UTF8ToUTF16(contents->GetVisibleURL().spec()));
    // The page has the keyboard, as a tab chosen in Chrome does.
    root_->FocusPage();
  }
  Refresh();
}

void Window::ShowNoPage(const std::u16string& address) {
  root_->ShowPage(nullptr);
  root_->SetAddress(address);
  Refresh();
}

content::WebContents* Window::selected() const {
  return root_ ? root_->page() : nullptr;
}

void Window::FocusAddress() {
  LogInteraction(Interaction::kFocusAddressField, compositor());
  root_->FocusAddress();
}

void Window::Refresh() {
  if (root_) {
    root_->SetRows(controller_->Rows());
  }
}

void Window::TabInserted(content::WebContents* contents,
                         content::WebContents* opener,
                         bool foreground) {
  controller_->TabInserted(contents, opener, foreground);
}

void Window::TabRemoved(content::WebContents* contents) {
  if (selected() == contents) {
    root_->ShowPage(nullptr);
  }
  controller_->TabRemoved(contents);
}

void Window::ActiveTabChanged(Browser* browser,
                              content::WebContents* contents) {
  if (contents) {
    controller_->ActiveTabChanged(contents);
  }
}

void Window::WindowWillClose() {
  root_->ShowPage(nullptr);
}

gfx::Rect Window::PageBounds() const {
  return root_ ? root_->PageBounds() : gfx::Rect();
}

bool Window::AcceleratorPressed(const ui::Accelerator& accelerator) {
  switch (accelerator.key_code()) {
    case ui::VKEY_T:
      controller_->NewTab();
      return true;
    case ui::VKEY_L:
      FocusAddress();
      return true;
    default:
      if (accelerator.key_code() >= ui::VKEY_1 &&
          accelerator.key_code() <= ui::VKEY_9) {
        controller_->SelectTabAt(accelerator.key_code() - ui::VKEY_0);
        return true;
      }
      return false;
  }
}

bool Window::CanHandleAccelerators() const {
  return host_ && host_->widget() && host_->widget()->IsActive();
}

content::KeyboardEventProcessingResult Window::PreHandleKeyboardEvent(
    const input::NativeWebKeyboardEvent& event) {
  views::FocusManager* focus = host_->widget()->GetFocusManager();
  const ui::Accelerator accelerator =
      ui::GetAcceleratorFromNativeWebKeyboardEvent(event);
  if (!focus->IsAcceleratorRegistered(accelerator)) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  if (IsAppFirst(accelerator)) {
    return focus->ProcessAccelerator(accelerator)
               ? content::KeyboardEventProcessingResult::HANDLED
               : content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  return content::KeyboardEventProcessingResult::NOT_HANDLED_IS_SHORTCUT;
}

bool Window::HandleKeyboardEvent(const input::NativeWebKeyboardEvent& event) {
  return unhandled_keys_.HandleKeyboardEvent(
      event, host_->widget()->GetFocusManager());
}

views::NativeWidget* Window::CreateNativeWidget(views::Widget* widget) {
  return CreateFrame(widget);
}

int Window::NonClientHitTest(const gfx::Point& point) {
  return root_ && root_->IsDragArea(point) ? HTCAPTION : HTNOWHERE;
}

void Window::RowPressed(const SidebarRow& row) {
  controller_->RowPressed(row);
}

void Window::AddressEntered(const std::u16string& text) {
  controller_->AddressEntered(text);
}

}  // namespace pane
