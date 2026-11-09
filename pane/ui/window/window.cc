// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/window.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "pane/ui/after_frame.h"
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
  // A foreground tab is shown by TabInserted, as Navigate inserts it.
  Navigate(&params);
  return params.navigated_or_inserted_contents;
}

void Window::Select(content::WebContents* contents) {
  if (contents != selected()) {
    LogInteraction(Interaction::kSelectTab, host_->widget()->GetCompositor());
  }
  // ⌘T's tab, chosen away from before its page came, opens behind.
  if (pending_tab_) {
    pending_tab_->foreground = false;
  }
  ShowTab(contents, /*chosen=*/true);
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

content::WebContents* Window::selected() const {
  return root_ ? root_->page() : nullptr;
}

std::string Window::selected_id() const {
  if (pending_tab_ && pending_tab_->foreground) {
    return pending_tab_->id;
  }
  content::WebContents* contents = selected();
  return contents ? TabIdOf(contents) : std::string();
}

void Window::TabInserted(content::WebContents* contents,
                         content::WebContents* opener,
                         bool foreground) {
  if (opening_tab_) {
    // ⌘T's tab keeps the address field what was typed in it meanwhile.
    AdoptOpeningTab(contents);
    if (foreground) {
      ShowTab(contents, /*chosen=*/false);
      return;
    }
  } else if (foreground) {
    Select(contents);
    return;
  }
  Refresh();
}

void Window::TabRemoved(content::WebContents* contents) {
  if (selected() == contents) {
    root_->ShowPage(nullptr);
  }
  Refresh();
}

void Window::ActiveTabChanged(Browser* browser,
                              content::WebContents* contents) {
  // Chrome tells its Browser, which tells this, before the window hears of
  // the insert.
  if (contents) {
    AdoptOpeningTab(contents);
  }
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
  ui::Compositor* compositor = host_->widget()->GetCompositor();
  LogInteraction(Interaction::kNewTab, compositor);
  Profile* profile = host_->active_profile();
  if (!profile) {
    return;
  }
  // One still waiting for its page gets it now, behind this one.
  if (pending_tab_) {
    pending_tab_->foreground = false;
    OpenPendingTab(pending_tab_->id, GURL(pending_tab_->url));
  }
  pending_tab_ = PendingTab{
      .id = NewTabId(), .profile = profile, .url = GURL(kNewTabURL).spec()};
  root_->ShowPage(nullptr);
  root_->SetAddress(base::UTF8ToUTF16(pending_tab_->url));
  // Typing goes to the address, as in Chrome: a key queued behind ⌘T lands
  // there.
  root_->FocusAddress();
  Refresh();
  AfterFrame(compositor,
             base::BindOnce(&Window::OpenPendingTab, weak_factory_.GetWeakPtr(),
                            pending_tab_->id, GURL(kNewTabURL)));
}

void Window::AdoptOpeningTab(content::WebContents* contents) {
  if (opening_tab_ && !TabWithId(opening_tab_->id)) {
    SetTabId(contents, opening_tab_->id);
  }
}

void Window::OpenPendingTab(std::string id, const GURL& url) {
  if (!pending_tab_ || pending_tab_->id != id) {
    return;
  }
  base::AutoReset<std::optional<PendingTab>> opening(
      &opening_tab_, std::exchange(pending_tab_, std::nullopt));
  // A page the engine couldn't make takes its row away.
  if (!OpenTab(opening_tab_->profile, url, opening_tab_->foreground)) {
    Refresh();
  }
}

void Window::FocusAddress() {
  LogInteraction(Interaction::kFocusAddressField,
                 host_->widget()->GetCompositor());
  root_->FocusAddress();
}

void Window::SelectTabAt(int number) {
  // ⌘T's tab, still waiting for its frame, is the profile's last: it gets
  // its page now, so the places count it.
  if (pending_tab_) {
    OpenPendingTab(pending_tab_->id, GURL(pending_tab_->url));
  }
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
  const GURL url(text);
  if (!url.is_valid()) {
    return;
  }
  // Entered before ⌘T's page came: it is made at the address entered.
  if (pending_tab_ && pending_tab_->foreground) {
    OpenPendingTab(pending_tab_->id, url);
    if (content::WebContents* contents = selected()) {
      contents->Focus();
    }
    return;
  }
  content::WebContents* contents = selected();
  if (!contents) {
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
