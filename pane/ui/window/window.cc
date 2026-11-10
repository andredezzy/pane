// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/window.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "pane/ui/app/app_events.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/window/frame.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/base/hit_test.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// A new window's size, as the Swift app's (ADR 0036).
constexpr gfx::Size kWindowSize(1280, 800);

// The keys the window takes before the page sees them: a page can't keep a
// user from a new tab or the address field (ADR 0019).
bool IsAppFirst(const ui::Accelerator& accelerator) {
  return accelerator.modifiers() == ui::EF_COMMAND_DOWN &&
         (accelerator.key_code() == ui::VKEY_T ||
          accelerator.key_code() == ui::VKEY_L);
}

}  // namespace

Window::Window(Controller* controller, bool keeps_frame)
    : controller_(controller), keeps_frame_(keeps_frame) {}

Window::~Window() {
  // A widget still there, not yet going: the host closes it.
  if (views::Widget* widget = root_ ? this->widget() : nullptr) {
    widget->RemoveObserver(this);
    if (views::FocusManager* focus = widget->GetFocusManager()) {
      focus->UnregisterAccelerators(this);
    }
  }
  root_ = nullptr;
  host_.reset();
}

void Window::Open() {
  if (host_) {
    return;
  }
  // The controller outlives the window.
  Controller* controller = controller_.get();
  RootView::Callbacks callbacks;
  SidebarView::Callbacks& sidebar = callbacks.sidebar;
  sidebar.toggle_profile = base::BindRepeating(&Controller::ToggleProfile,
                                               base::Unretained(controller));
  sidebar.select_tab =
      base::BindRepeating(&Controller::Select, base::Unretained(controller));
  sidebar.close_tab =
      base::BindRepeating(&Controller::CloseTab, base::Unretained(controller));
  sidebar.new_tab =
      base::BindRepeating(&Controller::NewTabIn, base::Unretained(controller));
  sidebar.edit_profile = base::BindRepeating(&Controller::EditProfile,
                                             base::Unretained(controller));
  sidebar.set_keep_loaded = base::BindRepeating(&Controller::SetKeepLoaded,
                                                base::Unretained(controller));
  sidebar.delete_profile = base::BindRepeating(&Controller::DeleteProfile,
                                               base::Unretained(controller));
  sidebar.collapse_all = base::BindRepeating(&Controller::CollapseAll,
                                             base::Unretained(controller));
  sidebar.new_profile = base::BindRepeating(&Controller::NewProfile,
                                            base::Unretained(controller));
  sidebar.toggle_settings = base::BindRepeating(&Controller::ToggleSettings,
                                                base::Unretained(controller));
  sidebar.move_profile = base::BindRepeating(&Controller::MoveProfile,
                                             base::Unretained(controller));
  sidebar.move_tab =
      base::BindRepeating(&Controller::MoveTab, base::Unretained(controller));
  callbacks.resize.resize = base::BindRepeating(&Controller::ResizeSidebar,
                                                base::Unretained(controller));
  callbacks.resize.end = base::BindRepeating(&Controller::EndSidebarResize,
                                             base::Unretained(controller));
  callbacks.resize.reset = base::BindRepeating(&Controller::ResetSidebarWidth,
                                               base::Unretained(controller));
  callbacks.address_entered = base::BindRepeating(&Controller::AddressEntered,
                                                  base::Unretained(controller));
  callbacks.back =
      base::BindRepeating(&Controller::Back, base::Unretained(controller));
  auto root = std::make_unique<RootView>(std::move(callbacks));
  root_ = root.get();
  host_ = std::make_unique<WindowHost>(this, gfx::Rect(kWindowSize),
                                       std::move(root));
  views::Widget* widget = host_->widget();
  widget->AddObserver(this);
  // The window's own fill, from the palette: each Theme change resolves it
  // again, and the Mac's window takes its appearance from it, so the lights
  // and the menus the window shows follow the Theme too.
  widget->SetBackgroundColor(kPaneColorWindowBackground);
  // In the middle of the screen, where AppKit puts a window it centers; then
  // where the last window was, if it keeps its frame and one was saved.
  widget->CenterWindow(kWindowSize);
  if (keeps_frame_) {
    KeepFrame(widget);
  }

  // The one command table: the window's FocusManager's accelerators.
  views::FocusManager* focus = widget->GetFocusManager();
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
  UpdateTitle();
}

void Window::Show(bool activate) {
  views::Widget* widget = this->widget();
  if (!widget) {
    return;
  }
  if (activate && MayTakeFront()) {
    widget->Show();
  } else {
    widget->ShowInactive();
  }
  Refresh();
}

void Window::Activate() {
  if (views::Widget* widget = this->widget(); widget && MayTakeFront()) {
    widget->Activate();
  }
}

views::Widget* Window::widget() const {
  return host_ ? host_->widget() : nullptr;
}

ui::Compositor* Window::compositor() const {
  views::Widget* widget = this->widget();
  return widget ? widget->GetCompositor() : nullptr;
}

Browser* Window::BrowserFor(Profile* profile) {
  return host_ ? host_->BrowserFor(profile) : nullptr;
}

void Window::ShowTab(content::WebContents* contents, bool chosen) {
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents);
  if (!browser || !root_) {
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
  UpdateTitle();
  Refresh();
}

void Window::ShowNoPage(const std::u16string& address) {
  if (!root_) {
    return;
  }
  ClearPage();
  root_->SetAddress(address);
  Refresh();
}

void Window::ClearPage() {
  if (!root_) {
    return;
  }
  root_->ShowPage(nullptr);
  UpdateTitle();
}

content::WebContents* Window::selected() const {
  return root_ ? root_->page() : nullptr;
}

void Window::UpdateTitle() {
  content::WebContents* page = selected();
  const std::u16string title = page ? page->GetTitle() : std::u16string();
  if (host_) {
    host_->SetTitle(title.empty() ? u"Pane" : title);
  }
}

bool Window::page_fullscreen() const {
  return root_ && root_->page_fullscreen();
}

void Window::FocusAddress() {
  if (!root_) {
    return;
  }
  LogInteraction(Interaction::kFocusAddressField, compositor());
  root_->FocusAddress();
}

void Window::Refresh() {
  if (root_) {
    root_->SetSidebar(controller_->Sidebar());
    root_->SetShows(controller_->Shows());
  }
}

void Window::SetSidebarWidth(double width) {
  if (root_) {
    root_->SetSidebarWidth(width);
  }
}

void Window::TabInserted(content::WebContents* contents,
                         content::WebContents* opener,
                         bool foreground) {
  controller_->TabInserted(contents, opener, foreground);
}

void Window::TabRemoved(content::WebContents* contents) {
  if (selected() == contents) {
    ClearPage();
  }
  controller_->TabRemoved(contents);
}

void Window::ActiveTabChanged(Browser* browser,
                              content::WebContents* contents) {
  if (contents) {
    controller_->ActiveTabChanged(contents);
  }
}

void Window::FullscreenChanged(content::WebContents* contents,
                               bool fullscreen) {
  // Chrome gives fullscreen to its Browser's active tab, which the window
  // shows; one leaving it may be going.
  if (!root_ || (fullscreen && contents != selected())) {
    return;
  }
  root_->SetPageFullscreen(fullscreen);
  FollowPage();
  Refresh();
}

void Window::FollowPage() {
  views::Widget* widget = this->widget();
  if (!widget || host_->closing()) {
    return;
  }
  const bool full_screen = widget->IsFullscreen();
  if (page_fullscreen()) {
    if (!full_screen) {
      full_screen_for_page_ = true;
      widget->SetFullscreen(true);
    }
  } else if (full_screen && full_screen_for_page_) {
    widget->SetFullscreen(false);
  }
}

// A move into or out of full screen ended. Leaving it, the user's or the
// page's, takes the page out of its fullscreen, which would otherwise fill a
// window with no sidebar or toolbar (ADR 0025).
void Window::OnWidgetShowStateChanged(views::Widget* widget) {
  if (widget->IsFullscreen()) {
    return;
  }
  full_screen_for_page_ = false;
  if (content::WebContents* page = selected(); page && page_fullscreen()) {
    page->ExitFullscreen(/*will_cause_resize=*/true);
  }
}

void Window::WindowWillClose() {
  ClearPage();
}

void Window::OnWidgetDestroying(views::Widget* widget) {
  widget->RemoveObserver(this);
  if (views::FocusManager* focus = widget->GetFocusManager()) {
    focus->UnregisterAccelerators(this);
  }
  // Its views go with it.
  root_ = nullptr;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&Window::Closed, weak_factory_.GetWeakPtr()));
}

void Window::Closed() {
  if (!host_ || root_) {
    return;
  }
  host_.reset();
  full_screen_for_page_ = false;
  controller_->WindowClosed();
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
  views::Widget* widget = this->widget();
  return widget && widget->IsActive();
}

content::KeyboardEventProcessingResult Window::PreHandleKeyboardEvent(
    const input::NativeWebKeyboardEvent& event) {
  views::Widget* widget = this->widget();
  if (!widget) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  views::FocusManager* focus = widget->GetFocusManager();
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
  views::Widget* widget = this->widget();
  return widget &&
         unhandled_keys_.HandleKeyboardEvent(event, widget->GetFocusManager());
}

views::NativeWidget* Window::CreateNativeWidget(views::Widget* widget) {
  return CreateFrame(widget);
}

int Window::NonClientHitTest(const gfx::Point& point) {
  return root_ && root_->IsDragArea(point) ? HTCAPTION : HTNOWHERE;
}

}  // namespace pane
