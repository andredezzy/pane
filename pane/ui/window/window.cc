// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/window.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/tabs/tab_id.h"
#include "pane/ui/window/frame.h"
#include "pane/ui/window/root_view.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/base/hit_test.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace pane {

namespace {

// What ⌘T opens, as the Swift app's AddressInput.newTabURL.
constexpr char kNewTabURL[] = "https://www.google.com";

// The keys the window takes before the page sees them: a page can't keep a
// user from a new tab or the address field (ADR 0019).
bool IsAppFirst(const ui::Accelerator& accelerator) {
  return accelerator.modifiers() == ui::EF_COMMAND_DOWN &&
         (accelerator.key_code() == ui::VKEY_T ||
          accelerator.key_code() == ui::VKEY_L);
}

}  // namespace

Window::Window(const gfx::Rect& bounds, Rows rows, Changed changed)
    : rows_(std::move(rows)), changed_(std::move(changed)) {
  auto root = std::make_unique<RootView>(
      base::BindRepeating(&Window::RowPressed, base::Unretained(this)),
      base::BindRepeating(&Window::AddressEntered, base::Unretained(this)));
  root_ = root.get();
  host_ = std::make_unique<WindowHost>(this, bounds, std::move(root));

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

content::WebContents* Window::OpenTab(Profile* profile,
                                      const GURL& url,
                                      bool foreground) {
  Browser* browser = host_->BrowserFor(profile);
  if (!browser) {
    return nullptr;
  }
  NavigateParams params(browser, url, ui::PAGE_TRANSITION_TYPED);
  params.disposition = foreground ? WindowOpenDisposition::kNewForegroundTab
                                  : WindowOpenDisposition::kNewBackgroundTab;
  params.window_action = NavigateParams::WindowAction::kNoAction;
  base::AutoReset<WindowHost::PendingOpen> pending(
      &host_->pending_open(),
      WindowHost::PendingOpen{params.disposition, nullptr});
  Navigate(&params);
  content::WebContents* contents = params.navigated_or_inserted_contents;
  if (contents && foreground) {
    Select(contents);
  }
  return contents;
}

void Window::Select(content::WebContents* contents) {
  if (contents != selected()) {
    LogInteraction(Interaction::kSelectTab, host_->widget()->GetCompositor());
  }
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
  root_->SetAddress(base::UTF8ToUTF16(contents->GetVisibleURL().spec()));
  Refresh();
}

content::WebContents* Window::selected() const {
  return root_ ? root_->page() : nullptr;
}

void Window::TabInserted(content::WebContents* contents,
                         content::WebContents* opener,
                         bool foreground) {
  if (foreground) {
    Select(contents);
  } else {
    Refresh();
  }
}

void Window::TabRemoved(content::WebContents* contents) {
  if (selected() == contents) {
    root_->ShowPage(nullptr);
  }
  Refresh();
}

void Window::ActiveTabChanged(Browser* browser,
                              content::WebContents* contents) {
  if (host_->IsActiveBrowser(browser) && contents) {
    root_->ShowPage(contents);
  }
  Refresh();
}

void Window::WindowWillClose() {
  root_->ShowPage(nullptr);
}

gfx::Rect Window::PageBounds() const {
  return root_ ? root_->PageBounds() : gfx::Rect();
}

void Window::NewTab() {
  LogInteraction(Interaction::kNewTab, host_->widget()->GetCompositor());
  if (Profile* profile = host_->active_profile()) {
    OpenTab(profile, GURL(kNewTabURL), /*foreground=*/true);
  }
}

void Window::FocusAddress() {
  LogInteraction(Interaction::kFocusAddressField,
                 host_->widget()->GetCompositor());
  root_->FocusAddress();
}

void Window::SelectTabAt(int number) {
  Browser* browser = host_->active_profile()
                         ? host_->ExistingBrowserFor(host_->active_profile())
                         : nullptr;
  if (!browser || browser->GetTabStripModel()->empty()) {
    return;
  }
  TabStripModel* model = browser->GetTabStripModel();
  const int index =
      number == 9 ? model->count() - 1 : std::min(number, model->count()) - 1;
  Select(model->GetWebContentsAt(index));
}

bool Window::AcceleratorPressed(const ui::Accelerator& accelerator) {
  switch (accelerator.key_code()) {
    case ui::VKEY_T:
      NewTab();
      return true;
    case ui::VKEY_L:
      FocusAddress();
      return true;
    default:
      if (accelerator.key_code() >= ui::VKEY_1 &&
          accelerator.key_code() <= ui::VKEY_9) {
        SelectTabAt(accelerator.key_code() - ui::VKEY_0);
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

void Window::AddressEntered(const std::u16string& text) {
  content::WebContents* contents = selected();
  const GURL url(text);
  if (!contents || !url.is_valid()) {
    return;
  }
  contents->GetController().LoadURL(url, content::Referrer(),
                                    ui::PAGE_TRANSITION_TYPED, std::string());
  contents->Focus();
}

void Window::RowPressed(const SidebarRow& row) {
  if (content::WebContents* contents = TabWithId(row.tab_id)) {
    Select(contents);
  }
}

void Window::Refresh() {
  if (!root_) {
    return;
  }
  root_->SetRows(rows_.Run(*this));
  if (changed_) {
    changed_.Run();
  }
}

}  // namespace pane
