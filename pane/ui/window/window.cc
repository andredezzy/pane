// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/window.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "pane/ui/after_frame.h"
#include "pane/ui/app/app_events.h"
#include "pane/ui/app/commands.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/tabs/tab_switcher_view.h"
#include "pane/ui/window/frame.h"
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

Window::Window(Controller* controller, bool keeps_frame)
    : controller_(controller), keeps_frame_(keeps_frame) {}

Window::~Window() {
  // A widget still there, not yet going: the host closes it.
  if (views::Widget* widget = root_ ? this->widget() : nullptr) {
    widget->RemoveObserver(this);
    if (views::FocusManager* focus = widget->GetFocusManager()) {
      focus->UnregisterAccelerators(this);
    }
    if (switcher_keys_) {
      widget->GetRootView()->RemovePreTargetHandler(switcher_keys_.get());
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
  sidebar.update = base::BindRepeating(&Controller::DownloadUpdate,
                                       base::Unretained(controller));
  sidebar.dismiss_update = base::BindRepeating(&Controller::DismissUpdate,
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
  ToolbarView::Callbacks& toolbar = callbacks.toolbar;
  toolbar.back =
      base::BindRepeating(&Controller::Back, base::Unretained(controller));
  toolbar.forward =
      base::BindRepeating(&Controller::Forward, base::Unretained(controller));
  toolbar.reload =
      base::BindRepeating(&Controller::Reload, base::Unretained(controller));
  toolbar.stop =
      base::BindRepeating(&Controller::Stop, base::Unretained(controller));
  toolbar.address_entered = base::BindRepeating(&Controller::AddressEntered,
                                                base::Unretained(controller));
  toolbar.address_left = base::BindRepeating(&Controller::AddressLeft,
                                             base::Unretained(controller));
  // The sad tab's Reload is the toolbar's, which makes a crashed tab's page
  // again.
  callbacks.reload_crashed =
      base::BindRepeating(&Controller::Reload, base::Unretained(controller));
  SettingsView::Callbacks& settings = callbacks.settings;
  settings.change = base::BindRepeating(&Controller::EditSettings,
                                        base::Unretained(controller));
  settings.choose_app_icon = base::BindRepeating(&Controller::OpenAppIconPicker,
                                                 base::Unretained(controller));
  settings.pin = base::BindRepeating(&Controller::ShowPinScreen,
                                     base::Unretained(controller));
  settings.check_updates = base::BindRepeating(&Controller::CheckForUpdates,
                                               base::Unretained(controller));
  settings.download_update = base::BindRepeating(&Controller::DownloadUpdate,
                                                 base::Unretained(controller));
  callbacks.choose_app_icon = base::BindRepeating(&Controller::ChooseAppIcon,
                                                  base::Unretained(controller));
  callbacks.close_app_icon_picker = base::BindRepeating(
      &Controller::CloseAppIconPicker, base::Unretained(controller));
  callbacks.cancel_quit = base::BindRepeating(&Controller::CancelQuit,
                                              base::Unretained(controller));
  callbacks.quit =
      base::BindRepeating(&Controller::QuitNow, base::Unretained(controller));
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
  UpdateTitleAfterFrame();
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
  if (switcher_keys_) {
    widget->GetRootView()->RemovePreTargetHandler(switcher_keys_.get());
    switcher_keys_.reset();
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
// bar takes Ctrl-Tab first). Any other key of the table goes to the page
// first, as a shortcut: unless the page takes it, HandleKeyboardEvent runs
// it.
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
  const ui::Accelerator accelerator =
      ui::GetAcceleratorFromNativeWebKeyboardEvent(event);
  const std::optional<Command> command = CommandForAccelerator(accelerator);
  if (!command) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  if (IsAppFirst(*command, page_fullscreen())) {
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
