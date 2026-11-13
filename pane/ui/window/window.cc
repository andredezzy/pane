// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/window.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "pane/ui/after_frame.h"
#include "pane/ui/app/app_events.h"
#include "pane/ui/app/commands.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/extensions/extensions_ui.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/tabs/tab_switcher_view.h"
#include "pane/ui/view_ids.h"
#include "pane/ui/window/frame.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/base/hit_test.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/event_handler.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// A new window's size, as the Swift app's (ADR 0036).
constexpr gfx::Size kWindowSize(1280, 800);

}  // namespace

// Ahead of the focused view and its own handling, the switcher's Esc, and
// Ctrl's own release, while it shows: Views' events come in order (the
// frame hears a real release first, and this one then finds the switcher
// gone; the channel's dispatch, with the window in the background, comes
// only here).
class Window::SwitcherKeys : public ui::EventHandler {
 public:
  explicit SwitcherKeys(Window* window) : window_(window) {}

  // ui::EventHandler:
  void OnKeyEvent(ui::KeyEvent* event) override {
    if (window_->SwitcherEscape(event->type() == ui::EventType::kKeyPressed,
                                event->key_code())) {
      event->SetHandled();
      return;
    }
    const ui::KeyboardCode code = event->key_code();
    if (event->type() == ui::EventType::kKeyReleased &&
        !event->IsControlDown() &&
        (code == ui::VKEY_CONTROL || code == ui::VKEY_LCONTROL ||
         code == ui::VKEY_RCONTROL)) {
      window_->ControlReleased();
    }
  }

 private:
  const raw_ptr<Window> window_;
};

// An extension's shortcut ahead of the view of Pane's that has the keys (the
// address being edited, ADR 0098), as Chrome's extension accelerators come
// ahead of its toolbar's: the shown profile's, unless an overlay takes every
// key (Quit Pane?).
class Window::ExtensionKeys : public ui::EventHandler {
 public:
  explicit ExtensionKeys(Window* window) : window_(window) {}

  // ui::EventHandler:
  void OnKeyEvent(ui::KeyEvent* event) override {
    if (event->type() != ui::EventType::kKeyPressed || !window_->root_ ||
        window_->root_->TakesEveryKey()) {
      return;
    }
    if (window_->controller_->Extensions().RunShortcut(
            ui::Accelerator(*event))) {
      event->SetHandled();
    }
  }

 private:
  const raw_ptr<Window> window_;
};

Window::Window(Controller* controller, bool keeps_frame)
    : controller_(controller), keeps_frame_(keeps_frame) {}

Window::~Window() {
  page_observation_.Reset();
  // A widget still there, not yet going: the host closes it.
  if (views::Widget* widget = root_ ? this->widget() : nullptr) {
    DetachFrom(widget);
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
  const auto bind = [controller](auto method) {
    return base::BindRepeating(method, base::Unretained(controller));
  };
  RootView::Callbacks callbacks;
  SidebarView::Callbacks& sidebar = callbacks.sidebar;
  sidebar.toggle_profile = bind(&Controller::ToggleProfile);
  sidebar.select_tab = bind(&Controller::Select);
  sidebar.close_tab = bind(&Controller::CloseTab);
  sidebar.new_tab = bind(&Controller::NewTabIn);
  sidebar.edit_profile = bind(&Controller::EditProfile);
  sidebar.set_keep_loaded = bind(&Controller::SetKeepLoaded);
  sidebar.delete_profile = bind(&Controller::DeleteProfile);
  sidebar.collapse_all = bind(&Controller::CollapseAll);
  sidebar.new_profile = bind(&Controller::NewProfile);
  sidebar.toggle_settings = bind(&Controller::ToggleSettings);
  sidebar.update = bind(&Controller::DownloadUpdate);
  sidebar.dismiss_update = bind(&Controller::DismissUpdate);
  sidebar.move_profile = bind(&Controller::MoveProfile);
  sidebar.move_tab = bind(&Controller::MoveTab);
  callbacks.resize.resize = bind(&Controller::ResizeSidebar);
  callbacks.resize.end = bind(&Controller::EndSidebarResize);
  callbacks.resize.reset = bind(&Controller::ResetSidebarWidth);
  ToolbarView::Callbacks& toolbar = callbacks.toolbar;
  toolbar.back = bind(&Controller::Back);
  toolbar.forward = bind(&Controller::Forward);
  toolbar.reload = bind(&Controller::Reload);
  toolbar.stop = bind(&Controller::Stop);
  toolbar.address_entered = bind(&Controller::AddressEntered);
  toolbar.address_left = bind(&Controller::AddressLeft);
  // The sad tab's Reload is the toolbar's, which makes a crashed tab's page
  // again.
  callbacks.reload_crashed = bind(&Controller::Reload);
  SettingsView::Callbacks& settings = callbacks.settings;
  settings.change = bind(&Controller::EditSettings);
  settings.choose_app_icon = bind(&Controller::OpenAppIconPicker);
  settings.pin = bind(&Controller::ShowPinScreen);
  settings.check_updates = bind(&Controller::CheckForUpdates);
  settings.download_update = bind(&Controller::DownloadUpdate);
  callbacks.choose_app_icon = bind(&Controller::ChooseAppIcon);
  callbacks.close_app_icon_picker = bind(&Controller::CloseAppIconPicker);
  callbacks.cancel_quit = bind(&Controller::CancelQuit);
  callbacks.quit = bind(&Controller::QuitNow);
  callbacks.find_bar.find = bind(&Controller::Find);
  callbacks.find_bar.step = bind(&Controller::FindStep);
  callbacks.find_bar.close = bind(&Controller::CloseFind);
  callbacks.extensions = &controller->Extensions();
  callbacks.profile_sheet.save = bind(&Controller::SaveProfileSheet);
  callbacks.profile_sheet.close = bind(&Controller::CloseProfileSheet);
  LockScreenView::Callbacks& lock = callbacks.lock;
  lock.digit = bind(&Controller::PinDigit);
  lock.backspace = bind(&Controller::PinBackspace);
  lock.continue_with_pin = bind(&Controller::ContinueWithPin);
  lock.back = bind(&Controller::DismissPinScreen);
  // Download, then Quit and install, as the pill's click.
  lock.update = bind(&Controller::DownloadUpdate);
  auto root = std::make_unique<RootView>(std::move(callbacks),
                                         &controller->LoadProgress(),
                                         &controller->AppIcons());
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

  // The one command table (commands.h): its keys are the window's
  // FocusManager's accelerators, as the menu bar's items show them. The
  // switcher's Esc and Ctrl's release come ahead of the focused view.
  views::FocusManager* focus = widget->GetFocusManager();
  for (const ui::Accelerator& accelerator : WindowAccelerators()) {
    focus->RegisterAccelerator(accelerator,
                               ui::AcceleratorManager::kNormalPriority, this);
  }
  switcher_keys_ = std::make_unique<SwitcherKeys>(this);
  widget->GetRootView()->AddPreTargetHandler(switcher_keys_.get());
  extension_keys_ = std::make_unique<ExtensionKeys>(this);
  widget->GetRootView()->AddPreTargetHandler(extension_keys_.get());
  page_observation_.Observe(root_->GetViewByID(kViewIdContent));
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
    // The page has the keyboard, as a tab chosen in Chrome does.
    root_->FocusPage();
  }
  // The tab's own find session, or none (Chrome's FindBarController).
  ShowFind();
  UpdateTitleAfterFrame();
  Refresh();
}

void Window::ShowNoPage() {
  if (!root_) {
    return;
  }
  // The title follows the frame that answers the input, as ShowTab's does:
  // AppKit lays its hidden title bar out again for each title.
  root_->ShowPage(nullptr);
  ShowFind();
  UpdateTitleAfterFrame();
  Refresh();
}

void Window::ClearPage() {
  if (!root_) {
    return;
  }
  root_->ShowPage(nullptr);
  ShowFind();
  UpdateTitle();
}

FindInPage* Window::ShownFind() const {
  content::WebContents* page = selected();
  BrowserWindowInterface* browser =
      page ? GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(page)
           : nullptr;
  FindInPage* find = FindInPage::From(browser);
  // Its controller finds in the Browser's active tab, which the window shows.
  return find && find->GetFindBarController() &&
                 find->GetFindBarController()->web_contents() == page
             ? find
             : nullptr;
}

void Window::ShowFind() {
  if (!root_) {
    return;
  }
  const FindInPage* find = ShownFind();
  root_->ShowFind(find ? find->state() : FindBarView::State());
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

void Window::UpdateTitleAfterFrame() {
  if (title_owed_) {
    return;
  }
  title_owed_ = true;
  // After what the frame let go, too: a tab's page made after it comes
  // first, which the title would hold back by as much.
  AfterFrame(compositor(),
             base::BindOnce(
                 [](base::WeakPtr<Window> window) {
                   base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                       FROM_HERE, base::BindOnce(
                                      [](base::WeakPtr<Window> window) {
                                        if (window) {
                                          window->title_owed_ = false;
                                          window->UpdateTitle();
                                        }
                                      },
                                      window));
                 },
                 weak_factory_.GetWeakPtr()));
}

bool Window::page_fullscreen() const {
  return root_ && root_->page_fullscreen();
}

bool Window::IsFullScreen() const {
  views::Widget* widget = this->widget();
  return widget && widget->IsFullscreen();
}

// The user's full screen: a page's fullscreen in it leaves it as it was, and
// leaving it takes the page out of its fullscreen (OnWidgetShowStateChanged).
void Window::ToggleFullScreen() {
  views::Widget* widget = this->widget();
  if (!widget || host_->closing()) {
    return;
  }
  widget->SetFullscreen(!widget->IsFullscreen());
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

// The window leaving the front closes the extensions menu, as the Swift's
// closed when its window resigned key.
void Window::OnWidgetActivationChanged(views::Widget* widget, bool active) {
  if (!active) {
    controller_->Extensions().CloseMenu();
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

void Window::ShowExclusiveAccessHint(const std::u16string& hint) {
  if (root_) {
    root_->ShowExclusiveAccessHint(hint);
  }
}

std::unique_ptr<FindBar> Window::CreateFindBar(Browser* browser) {
  return std::make_unique<FindInPage>(weak_factory_.GetWeakPtr(), *browser);
}

std::unique_ptr<HostedExtensionsContainer> Window::CreateExtensionsContainer(
    Browser* browser) {
  return controller_->Extensions().MakeContainer(*browser);
}

// At each step of DevTools' splitter too: the page and DevTools are laid out
// again, and nothing else of the window's changes.
void Window::DevToolsChanged(content::WebContents* inspected) {
  if (root_ && inspected && selected() == inspected) {
    root_->UpdateDevTools();
  }
}

bool Window::HandleContextMenu(content::RenderFrameHost& frame,
                               const content::ContextMenuParams& params) {
  controller_->ShowContextMenu(frame, params);
  return true;
}

void Window::ShowRequested(bool active) {
  Show(active);
}

void Window::FindChanged(FindInPage* find) {
  if (root_ && find == ShownFind()) {
    root_->ShowFind(find->state());
  }
}

void Window::FocusFind(FindInPage* find) {
  if (root_ && find == ShownFind()) {
    root_->FocusFind();
  }
}

// The bar closed: the page takes the keys back, as the Swift's closeFind
// gave them, unless they went elsewhere meanwhile.
void Window::FindClosed(FindInPage* find) {
  if (!root_ || find != ShownFind()) {
    return;
  }
  views::FocusManager* focus = widget() ? widget()->GetFocusManager() : nullptr;
  const views::View* focused = focus ? focus->GetFocusedView() : nullptr;
  if (!focused || root_->FindHasFocus() ||
      focused->GetID() == kViewIdFindClose) {
    root_->FocusPage();
  }
}

bool Window::FindHasFocus(const FindInPage* find) const {
  return root_ && find == ShownFind() && root_->FindHasFocus();
}

::ui::Compositor* Window::FindCompositor() {
  return compositor();
}

void Window::WindowWillClose() {
  ClearPage();
}

void Window::OnViewBoundsChanged(views::View* observed_view) {
  if (host_) {
    host_->ScheduleDialogPositionUpdate();
  }
}

void Window::OnWidgetDestroying(views::Widget* widget) {
  DetachFrom(widget);
  // Its views go with it.
  root_ = nullptr;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&Window::Closed, weak_factory_.GetWeakPtr()));
}

void Window::DetachFrom(views::Widget* widget) {
  page_observation_.Reset();
  widget->RemoveObserver(this);
  if (views::FocusManager* focus = widget->GetFocusManager()) {
    focus->UnregisterAccelerators(this);
  }
  if (switcher_keys_) {
    widget->GetRootView()->RemovePreTargetHandler(switcher_keys_.get());
    switcher_keys_.reset();
  }
  if (extension_keys_) {
    widget->GetRootView()->RemovePreTargetHandler(extension_keys_.get());
    extension_keys_.reset();
  }
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
  const std::optional<Command> command = CommandForAccelerator(accelerator);
  if (!command || IsPlatformCommand(*command) ||
      !controller_->IsCommandEnabled(*command)) {
    return false;
  }
  controller_->RunCommand(*command);
  return true;
}

// While an overlay takes every key (Quit Pane?), none of the window's
// commands runs from its key.
bool Window::CanHandleAccelerators() const {
  views::Widget* widget = this->widget();
  return widget && widget->IsActive() && root_ && !root_->TakesEveryKey();
}

bool Window::SwitcherEscape(bool pressed, int key_code) {
  TabSwitcherView* switcher = root_ ? root_->switcher() : nullptr;
  // Esc, with whatever modifier is down: it closes, taking the key.
  if (!switcher || !switcher->GetVisible() || !pressed ||
      key_code != ui::VKEY_ESCAPE) {
    return false;
  }
  controller_->CancelSwitcher();
  return true;
}

void Window::ControlReleased() {
  TabSwitcherView* switcher = root_ ? root_->switcher() : nullptr;
  if (switcher && switcher->GetVisible()) {
    controller_->CommitSwitcher();
  }
}

// A key reaches the page after the window has seen it: the switcher's Esc
// while it shows, then a command that goes ahead of the page, which the page
// never sees: what Chrome reserves (IsAppFirst, ADR 0019; on the Mac the menu
// bar takes Ctrl-Tab first); then an extension's shortcut, ahead of the page
// as Chrome's extension accelerators are (ADR 0098). Any other key of the
// table goes to the page first, as a shortcut: unless the page takes it,
// HandleKeyboardEvent runs it. While Chrome's shortcuts page records a key,
// the page has every key first, as Chrome's window lets it
// (WillExecuteCommand): a command runs only if the page leaves it.
content::KeyboardEventProcessingResult Window::PreHandleKeyboardEvent(
    const input::NativeWebKeyboardEvent& event) {
  views::Widget* widget = this->widget();
  if (!widget || !root_) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  const bool pressed =
      event.GetType() == input::NativeWebKeyboardEvent::Type::kRawKeyDown;
  if (SwitcherEscape(pressed, event.windows_key_code)) {
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  if (!pressed) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  // The extensions menu's Esc, and a page's dialog's Esc and Return, ahead
  // of the page as ahead of a focused view of Pane's.
  if ((event.windows_key_code == ui::VKEY_ESCAPE ||
       event.windows_key_code == ui::VKEY_RETURN) &&
      !(event.GetModifiers() &
        (blink::WebInputEvent::kControlKey | blink::WebInputEvent::kAltKey |
         blink::WebInputEvent::kMetaKey)) &&
      root_->OverlayKey(
          ui::KeyEvent(ui::EventType::kKeyPressed,
                       static_cast<ui::KeyboardCode>(event.windows_key_code),
                       ui::EF_NONE))) {
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  const ui::Accelerator accelerator =
      ui::GetAcceleratorFromNativeWebKeyboardEvent(event);
  const std::optional<Command> command = CommandForAccelerator(accelerator);
  ExtensionsUi& extensions = controller_->Extensions();
  if (!command) {
    return !root_->TakesEveryKey() && extensions.RunShortcut(accelerator)
               ? content::KeyboardEventProcessingResult::HANDLED
               : content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  if (IsAppFirst(*command, page_fullscreen()) &&
      !extensions.ShortcutsSuspended()) {
    return widget->GetFocusManager()->ProcessAccelerator(accelerator)
               ? content::KeyboardEventProcessingResult::HANDLED
               : content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  return content::KeyboardEventProcessingResult::NOT_HANDLED_IS_SHORTCUT;
}

// A key the page left: the window's command for it, else, on the Mac, the
// menu bar's (the Edit menu's Copy, the window's Minimize) through the
// window's own dispatch.
bool Window::HandleKeyboardEvent(const input::NativeWebKeyboardEvent& event) {
  views::Widget* widget = this->widget();
  return widget &&
         unhandled_keys_.HandleKeyboardEvent(event, widget->GetFocusManager());
}

views::NativeWidget* Window::CreateNativeWidget(views::Widget* widget) {
  return CreateFrame(widget,
                     base::BindRepeating(
                         [](base::WeakPtr<Window> window) {
                           return window && window->root_ &&
                                  window->root_->TakesEveryKey();
                         },
                         weak_factory_.GetWeakPtr()),
                     base::BindRepeating(
                         [](base::WeakPtr<Window> window, bool control_down) {
                           if (window && !control_down) {
                             window->ControlReleased();
                           }
                         },
                         weak_factory_.GetWeakPtr()));
}

int Window::NonClientHitTest(const gfx::Point& point) {
  return root_ && root_->IsDragArea(point) ? HTCAPTION : HTNOWHERE;
}

}  // namespace pane
