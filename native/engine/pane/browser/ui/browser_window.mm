// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Adapted from Ardia (https://github.com/mantrakp04/ardia),
// engine/ardiacore/src/ardia/core/ac_browser_window.mm, Copyright 2026 Mantra Patel,
// licensed under the Apache License 2.0 (native/third_party/ardia). Changed
// for Pane: names and namespace, and without Ardia's extension shortcuts, autofill prompts and background-run eye dropper; Chrome's own shutdown checks for its lifetime helpers.

#include "pane/browser/extensions/action_popup.h"
#include "pane/browser/ui/browser_window.h"

#import <AppKit/AppKit.h>

#include <stdlib.h>
#include <string.h>

#include <map>
#include <set>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/unload_controller.h"
#include "chrome/app/chrome_command_ids.h"

#include "chrome/browser/devtools/devtools_ui_controller.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/autofill/save_address_bubble_controller.h"
#include "chrome/browser/ui/autofill/update_address_bubble_controller.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/find_bar/find_bar.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "ui/gfx/range/range.h"
#include "chrome/browser/ui/views/bubble_anchor_util_views.h"
#include "chrome/browser/ui/views/exclusive_access/exclusive_access_bubble_views.h"
#include "chrome/browser/ui/views/eye_dropper/eye_dropper.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/eye_dropper.h"
#include "content/public/browser/eye_dropper_listener.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#import "pane/public/PaneBrowserWindow.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/ui/window_host.h"
#include "pane/browser/ui/modal_dialog_host.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/base/ui_base_features.h"
#include "ui/display/types/display_constants.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/base/mojom/window_show_state.mojom.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/mac/coordinate_conversion.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/widget/widget.h"

namespace pane {

HostedLocationBar::HostedLocationBar() : LocationBar(nullptr) {}
HostedLocationBar::~HostedLocationBar() = default;
OmniboxView* HostedLocationBar::GetOmniboxView() {
  return nullptr;
}
OmniboxPopupView* HostedLocationBar::GetOmniboxPopupView() {
  return nullptr;
}
OmniboxController* HostedLocationBar::GetOmniboxController() {
  return nullptr;
}
bool HostedLocationBar::ShouldCloseOmniboxPopup(ui::MouseEvent* event) {
  return false;
}
content::WebContents* HostedLocationBar::GetWebContents() {
  return browser_ ? browser_->GetTabStripModel()->GetActiveWebContents()
                  : nullptr;
}
LocationBarModel* HostedLocationBar::GetLocationBarModel() {
  return nullptr;
}
std::optional<bubble_anchor_util::AnchorConfiguration>
HostedLocationBar::GetChipAnchor() {
  return std::nullopt;
}
ChipController* HostedLocationBar::GetChipController() {
  return nullptr;
}
ui::TrackedElement* HostedLocationBar::GetAnchorOrNull() {
  return nullptr;
}
BrowserWindowInterface* HostedLocationBar::GetBrowser() {
  return browser_;
}
Profile* HostedLocationBar::GetProfile() {
  return browser_ ? browser_->GetProfile() : nullptr;
}
bool HostedLocationBar::IsInitialized() const {
  return true;
}
bool HostedLocationBar::IsVisible() const {
  return false;
}
bool HostedLocationBar::IsDrawn() const {
  return false;
}
bool HostedLocationBar::IsFullscreen() const {
  return false;
}
bool HostedLocationBar::IsEditingOrEmpty() const {
  return false;
}
bool HostedLocationBar::IsMouseHovered() const {
  return false;
}
bool HostedLocationBar::IsFocusWithin() const {
  return false;
}
gfx::Rect HostedLocationBar::Bounds() const {
  return gfx::Rect();
}
gfx::Rect HostedLocationBar::BoundsInScreen() const {
  return gfx::Rect();
}
gfx::Size HostedLocationBar::MinimumSize() const {
  return gfx::Size();
}
gfx::Size HostedLocationBar::PreferredSize() const {
  return gfx::Size();
}
bool HostedLocationBar::HasSecurityStateChanged() {
  return false;
}
LocationBarTesting* HostedLocationBar::GetLocationBarForTesting() {
  return nullptr;
}

HostedAutofillBubbleHandler::HostedAutofillBubbleHandler() = default;
HostedAutofillBubbleHandler::~HostedAutofillBubbleHandler() = default;

#define PANE_NO_BUBBLE(...) \
  {                       \
    return nullptr;       \
  }
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::ShowSaveCreditCardBubble(
    content::WebContents* web_contents,
    autofill::SaveCardBubbleController* controller,
    bool) {
  return nullptr;
}
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::ShowIbanBubble(
    content::WebContents* web_contents,
    autofill::IbanBubbleController*,
    bool,
    autofill::IbanBubbleType) {
  return nullptr;
}
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowOfferNotificationBubble(content::WebContents* web_contents,
                                autofill::OfferNotificationBubbleController*,
                                bool) {
  return nullptr;
}
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowSaveAutofillAiDataBubble(content::WebContents*,
                                 autofill::AutofillAiImportDataController*)
        PANE_NO_BUBBLE()
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowAutofillAiLocalSaveNotification(
        content::WebContents*,
        autofill::AutofillAiImportDataController*) PANE_NO_BUBBLE()
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowSaveAddressProfileBubble(
        content::WebContents* web_contents,
        std::unique_ptr<autofill::SaveAddressBubbleController> controller,
        bool) {
  return nullptr;
}
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::ShowAddressSignInPromo(
    content::WebContents* web_contents,
    const autofill::AutofillProfile&) {
  return nullptr;
}
#endif
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowUpdateAddressProfileBubble(
        content::WebContents* web_contents,
        std::unique_ptr<autofill::UpdateAddressBubbleController> controller,
        bool) {
  return nullptr;
}
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowFilledCardInformationBubble(
        content::WebContents*,
        autofill::FilledCardInformationBubbleController*,
        bool) PANE_NO_BUBBLE()
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowVirtualCardEnrollBubble(content::WebContents*,
                                autofill::VirtualCardEnrollBubbleController*,
                                bool) PANE_NO_BUBBLE()
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowVirtualCardEnrollConfirmationBubble(
        content::WebContents*,
        autofill::VirtualCardEnrollBubbleController*) PANE_NO_BUBBLE()
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowMandatoryReauthBubble(content::WebContents*,
                              autofill::MandatoryReauthBubbleController*,
                              bool,
                              autofill::MandatoryReauthBubbleType)
        PANE_NO_BUBBLE()
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowSaveCardConfirmationBubble(
        content::WebContents* web_contents,
        autofill::SaveCardBubbleController* controller) {
  return nullptr;
}
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowSaveIbanConfirmationBubble(content::WebContents*,
                                   autofill::IbanBubbleController*)
        PANE_NO_BUBBLE()
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowOmniboxAutofillBubble(content::WebContents*,
                              autofill::OmniboxAutofillBubbleController*)
        PANE_NO_BUBBLE()
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowPaymentsChurnedUsersBubble(
        content::WebContents*,
        autofill::PaymentsChurnedUsersBubbleController*,
        bool) PANE_NO_BUBBLE()
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowPaymentsChurnedUsersConfirmationBubble(
        content::WebContents*,
        autofill::PaymentsChurnedUsersBubbleController*) PANE_NO_BUBBLE()
autofill::AutofillBubbleBase* HostedAutofillBubbleHandler::
    ShowWalletReminderNoticeBubble(
        content::WebContents*,
        autofill::WalletReminderNoticeBubbleController*,
        bool) PANE_NO_BUBBLE()
#undef PANE_NO_BUBBLE

namespace {

std::set<const BrowserWindow*>& LiveWindows() {
  static base::NoDestructor<std::set<const BrowserWindow*>> windows;
  return *windows;
}
std::map<const BrowserWindowInterface*, base::WeakPtr<WindowHost>>& Records() {
  static base::NoDestructor<
      std::map<const BrowserWindowInterface*, base::WeakPtr<WindowHost>>>
      records;
  return *records;
}

}

HostedBrowserWindow* HostedBrowserWindow::FromWindow(const BrowserWindow* window) {
  if (!window || !LiveWindows().contains(window)) {
    return nullptr;
  }
  return static_cast<HostedBrowserWindow*>(const_cast<BrowserWindow*>(window));
}

void HostedBrowserWindow::Register(const BrowserWindowInterface* browser) {
  Records()[browser] = host_;
}

bool HostedBrowserWindow::IsOurs(const BrowserWindowInterface* browser) {
  return browser && Records().contains(browser);
}

WindowHost* HostedBrowserWindow::HostOf(const BrowserWindowInterface* browser) {
  auto it = browser ? Records().find(browser) : Records().end();
  return it == Records().end() ? nullptr : it->second.get();
}

void HostedBrowserWindow::Forget(const BrowserWindowInterface* browser) {
  Records().erase(browser);
}

HostedBrowserWindow::HostedBrowserWindow(base::WeakPtr<WindowHost> host)
    : host_(std::move(host)), location_bar_(std::make_unique<HostedLocationBar>()) {
  LiveWindows().insert(this);
}

// The Browser goes right after its window. An extension popup opened for it
// goes first, and the Browser stops counting as Pane's last, so a later one
// at the same address is not taken for it.
HostedBrowserWindow::~HostedBrowserWindow() {
  LiveWindows().erase(this);
  if (browser_) {
    ActionPopup::CloseAll(browser_);
  }
  DestroyAnyExclusiveAccessBubble();
  devtools_ui_controller_.reset();
  autofill_bubble_handler_registration_.reset();
  if (browser_) {
    browser_->GetFeatures().TearDownPreBrowserWindowDestruction();
  }
  if (host_) {
    host_->BrowserWindowDestroyed(this);
  }
  if (browser_) {
    Forget(browser_);
  }
}

void HostedBrowserWindow::AttachBrowser(Browser* browser) {
  browser_ = browser;
  Register(browser);
  autofill_bubble_handler_registration_.emplace(
      browser->GetUnownedUserDataHost(), autofill_bubble_handler_);
  location_bar_->set_browser(browser);
  devtools_ui_controller_ = std::make_unique<DevtoolsUIController>(
      browser,
      std::vector<raw_ptr<ContentsContainerView, DanglingUntriaged>>());
}

void HostedBrowserWindow::DeleteBrowserWindow() {
  delete this;
}

bool HostedBrowserWindow::IsActive() const {
  if (!host_ || !host_->widget()) {
    return false;
  }
  return host_->widget()->IsActive() && host_->IsActiveBrowser(browser_);
}

bool HostedBrowserWindow::IsMaximized() const {
  return host_ && host_->widget() && host_->widget()->IsMaximized();
}

bool HostedBrowserWindow::IsMinimized() const {
  return host_ && host_->widget() && host_->widget()->IsMinimized();
}

bool HostedBrowserWindow::IsFullscreen() const {
  if (tab_fullscreen_ ||
      (host_ && (host_->acted_fullscreen() ||
                 (host_->widget() && host_->widget()->IsFullscreen())))) {
    return true;
  }
  ExclusiveAccessManager* manager =
      browser_ ? ExclusiveAccessManager::From(browser_) : nullptr;
  return manager && manager->fullscreen_controller()->IsTabFullscreen();
}

gfx::NativeWindow HostedBrowserWindow::GetNativeWindow() const {
  return host_ && host_->widget() ? host_->widget()->GetNativeWindow()
                                  : gfx::NativeWindow();
}

gfx::Rect HostedBrowserWindow::GetRestoredBounds() const {
  return host_ && host_->widget() ? host_->widget()->GetRestoredBounds()
                                  : gfx::Rect();
}

ui::mojom::WindowShowState HostedBrowserWindow::GetRestoredState() const {
  return ui::mojom::WindowShowState::kNormal;
}

gfx::Rect HostedBrowserWindow::GetBounds() const {
  return host_ && host_->widget() ? host_->widget()->GetWindowBoundsInScreen()
                                  : gfx::Rect();
}

namespace {

// Chrome asks to show a Browser's window (a tab it opened, an extension):
// bring Pane's window forward unless the app has hidden it.
void RequestShow(WindowHost* host, bool active) {
  NSWindow* window = host ? host->ns_window() : nil;
  if (!window || window.visible) {
    return;
  }
  if (active) {
    [window makeKeyAndOrderFront:nil];
  } else {
    [window orderFront:nil];
  }
}

}

void HostedBrowserWindow::Show() {
  RequestShow(host_.get(), true);
}

bool HostedBrowserWindow::IsVisible() const {
  return host_ && host_->widget() && host_->widget()->IsVisible() &&
         host_->IsActiveBrowser(browser_);
}

void HostedBrowserWindow::ShowInactive() {
  RequestShow(host_.get(), false);
}

void HostedBrowserWindow::Close() {
  if (!browser_) {
    return;
  }
  Profile* profile = browser_->GetProfile();
  closed_by_chrome_ = host_ && !host_->closing() && !browser_shutdown::IsTryingToQuit() &&
                      !profile->ShutdownStarted();
  UnloadController* unload = UnloadController::From(browser_);
  if (unload) {
    unload->OnWindowClosing();
  }
}

void HostedBrowserWindow::Activate() {
  if (host_ && browser_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<WindowHost> host,
                          base::WeakPtr<BrowserWindowInterface> browser) {
                         if (host && browser) {
                           host->RequestActivation(static_cast<Browser*>(browser.get()));
                         }
                       },
                       host_, browser_->GetWeakPtr()));
  }
}

void HostedBrowserWindow::SetBounds(const gfx::Rect& bounds) {
  if (host_ && host_->widget()) {
    host_->widget()->SetBounds(bounds);
  }
}

ui::ZOrderLevel HostedBrowserWindow::GetZOrderLevel() const {
  return ui::ZOrderLevel::kNormal;
}

bool HostedBrowserWindow::IsOnCurrentWorkspace() const {
  return host_ && !host_->internal();
}

bool HostedBrowserWindow::IsVisibleOnScreen() const {
  return IsVisible();
}

bool HostedBrowserWindow::DoBrowserControlsShrinkRendererSize(
    const content::WebContents* contents) const {
  return false;
}

ui::NativeTheme* HostedBrowserWindow::GetNativeTheme() {
  return ui::NativeTheme::GetInstanceForNativeUi();
}

const ui::ThemeProvider* HostedBrowserWindow::GetThemeProvider() const {
  return browser_ ? &ThemeService::GetThemeProviderForProfile(
                        browser_->GetProfile())
                  : nullptr;
}

const ui::ColorProvider* HostedBrowserWindow::GetColorProvider() const {
  if (host_ && host_->widget()) {
    return host_->widget()->GetColorProvider();
  }
  return ui::ColorProviderManager::Get().GetColorProviderFor(
      ui::ColorProviderKey());
}

int HostedBrowserWindow::GetTopControlsHeight() const {
  return 0;
}

std::vector<StatusBubble*> HostedBrowserWindow::GetStatusBubbles() {
  return {};
}

void HostedBrowserWindow::OnActiveTabChanged(content::WebContents* old_contents,
                                         content::WebContents* new_contents,
                                         int index,
                                         int reason) {
  if (host_ && browser_) {
    host_->ActiveTabChanged(browser_, new_contents);
  }
}

gfx::Size HostedBrowserWindow::GetContentsSize() const {
  if (!host_ || !host_->host_view()) {
    return gfx::Size();
  }
  NSSize size = host_->host_view().bounds.size;
  return gfx::Size(size.width, size.height);
}

autofill::AutofillBubbleHandler* HostedBrowserWindow::GetAutofillBubbleHandler() {
  return &autofill_bubble_handler_;
}

PinnedToolbarActions* HostedBrowserWindow::GetPinnedToolbarActions() {
  return nullptr;
}

LocationBar* HostedBrowserWindow::GetLocationBar() const {
  return location_bar_.get();
}

bool HostedBrowserWindow::UpdateToolbarSecurityState() {
  return false;
}

bool HostedBrowserWindow::IsTabStripEditable() const {
  return true;
}

bool HostedBrowserWindow::IsToolbarVisible() const {
  return false;
}

bool HostedBrowserWindow::IsToolbarShowing() const {
  return false;
}

bool HostedBrowserWindow::IsLocationBarVisible() const {
  return false;
}

ShowTranslateBubbleResult HostedBrowserWindow::ShowTranslateBubble(
    content::WebContents* contents,
    translate::TranslateStep step,
    const std::string& source_language,
    const std::string& target_language,
    translate::TranslateErrors error_type,
    bool is_user_gesture) {
  return ShowTranslateBubbleResult::kBrowserWindowNotValid;
}

DownloadBubbleUIController* HostedBrowserWindow::GetDownloadBubbleUIController() {
  return nullptr;
}

void HostedBrowserWindow::ConfirmBrowserCloseWithPendingDownloads(
    int download_count,
    DownloadCloseType dialog_type,
    base::OnceCallback<void(bool)> callback) {
  if (host_ && !host_->closing() && !browser_shutdown::IsTryingToQuit()) {
    host_->ConfirmCloseWithDownloads(download_count, std::move(callback));
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), true));
}

namespace {

bool MenuHasKeyEquivalent(NSMenu* menu, NSEvent* event) {
  const NSEventModifierFlags mask =
      NSEventModifierFlagCommand | NSEventModifierFlagOption |
      NSEventModifierFlagControl | NSEventModifierFlagShift;
  const NSEventModifierFlags flags = event.modifierFlags & mask;
  NSString* chars = event.charactersIgnoringModifiers.lowercaseString;
  for (NSMenuItem* item in menu.itemArray) {
    if (item.hasSubmenu) {
      if (MenuHasKeyEquivalent(item.submenu, event)) {
        return true;
      }
      continue;
    }
    NSString* key = item.keyEquivalent;
    if (!key.length) {
      continue;
    }
    NSEventModifierFlags item_flags = item.keyEquivalentModifierMask & mask;
    if (![key isEqualToString:key.lowercaseString]) {
      item_flags |= NSEventModifierFlagShift;
    }
    if (item_flags == flags && [key.lowercaseString isEqualToString:chars]) {
      return true;
    }
  }
  return false;
}

NSEvent* UnhandledKeyDown(const input::NativeWebKeyboardEvent& event) {
  if (event.skip_if_unhandled ||
      event.GetType() == input::NativeWebKeyboardEvent::Type::kChar) {
    return nil;
  }
  NSEvent* ns_event = event.os_event.Get();
  return ns_event.type == NSEventTypeKeyDown ? ns_event : nil;
}

}

content::KeyboardEventProcessingResult HostedBrowserWindow::PreHandleKeyboardEvent(
    const input::NativeWebKeyboardEvent& event) {
  // Esc leaves fullscreen and pointer or keyboard lock, as in Chrome.
  if (ExclusiveAccessManager* manager = GetExclusiveAccessManager();
      manager && manager->HandleUserKeyEvent(event)) {
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  NSEvent* ns_event = event.os_event.Get();
  if (!ns_event || ns_event.type != NSEventTypeKeyDown || !host_) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  PaneBrowserWindow* owner = host_->owner();
  id<PaneBrowserWindowDelegate> delegate = owner.delegate;
  if ([delegate respondsToSelector:@selector(browserWindow:preHandleKeyEvent:)] &&
      [delegate browserWindow:owner preHandleKeyEvent:ns_event]) {
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  if ((ns_event.modifierFlags & NSEventModifierFlagCommand) &&
      MenuHasKeyEquivalent(NSApp.mainMenu, ns_event)) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED_IS_SHORTCUT;
  }
  return content::KeyboardEventProcessingResult::NOT_HANDLED;
}

bool HostedBrowserWindow::HandleKeyboardEvent(
    const input::NativeWebKeyboardEvent& event) {
  NSEvent* ns_event = UnhandledKeyDown(event);
  if (!ns_event || !host_) {
    return false;
  }
  PaneBrowserWindow* owner = host_->owner();
  id<PaneBrowserWindowDelegate> delegate = owner.delegate;
  if ([delegate respondsToSelector:@selector(browserWindow:handleKeyEvent:)] &&
      [delegate browserWindow:owner handleKeyEvent:ns_event]) {
    return true;
  }
  if (!(ns_event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl |
                                  NSEventModifierFlagFunction))) {
    return false;
  }
  return [NSApp.mainMenu performKeyEquivalent:ns_event];
}

std::unique_ptr<FindBar> HostedBrowserWindow::CreateFindBar() {
  return std::make_unique<HostedFindBar>();
}

HostedFindBar::HostedFindBar() = default;
HostedFindBar::~HostedFindBar() = default;
FindBarController* HostedFindBar::GetFindBarController() const {
  return controller_;
}
void HostedFindBar::SetFindBarController(FindBarController* controller) {
  controller_ = controller;
}
void HostedFindBar::Show(bool animate, bool focus) {
  visible_ = true;
}
void HostedFindBar::Hide(bool animate) {
  visible_ = false;
}
void HostedFindBar::SetFindTextAndSelectedRange(const std::u16string& find_text,
                                            const gfx::Range& selected_range) {
  find_text_ = find_text;
}
std::u16string_view HostedFindBar::GetFindText() const {
  return find_text_;
}
gfx::Range HostedFindBar::GetSelectedRange() const {
  return gfx::Range();
}
bool HostedFindBar::IsFindBarVisible() const {
  return visible_;
}
bool HostedFindBar::HasGlobalFindPasteboard() const {
  return true;
}
bool HostedFindBar::CanPopulateFromSelectedText() {
  return true;
}
const FindBarTesting* HostedFindBar::GetFindBarTesting() const {
  return nullptr;
}
bool HostedFindBar::HasFocus() const {
  return false;
}
views::Widget* HostedFindBar::GetHostWidget() {
  return nullptr;
}

web_modal::WebContentsModalDialogHost*
HostedBrowserWindow::GetWebContentsModalDialogHost() {
  return host_.get();
}

web_modal::WebContentsModalDialogHost*
HostedBrowserWindow::GetWebContentsModalDialogHostFor(
    content::WebContents* web_contents) {
  if (web_contents && tabs::TabInterface::MaybeGetFromContents(web_contents)) {
    return TabDialogHost::GetOrCreateForWebContents(web_contents);
  }
  return host_.get();
}

ExclusiveAccessContext* HostedBrowserWindow::GetExclusiveAccessContext() {
  return this;
}

Profile* HostedBrowserWindow::GetProfile() {
  return browser_ ? browser_->GetProfile() : nullptr;
}

void HostedBrowserWindow::EnterFullscreen(const url::Origin& origin,
                                      ExclusiveAccessBubbleType bubble_type,
                                      FullscreenTabParams fullscreen_tab_params) {
  tab_fullscreen_ = true;
  base::WeakPtr<HostedBrowserWindow> self = weak_factory_.GetWeakPtr();
  ReportFullscreenTab();
  if (self) {
    PostWindowFullscreenStateChanged();
  }
}

void HostedBrowserWindow::ExitFullscreen() {
  tab_fullscreen_ = false;
  ReportFullscreenTab();
}

void HostedBrowserWindow::PostWindowFullscreenStateChanged() {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<HostedBrowserWindow> window) {
                       ExclusiveAccessManager* manager =
                           window ? window->GetExclusiveAccessManager()
                                  : nullptr;
                       if (manager && window->tab_fullscreen_) {
                         manager->fullscreen_controller()
                             ->WindowFullscreenStateChanged();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

// The "Press Esc to exit full screen" hint is the app's to show
// (browserWindow:showExclusiveAccessHint:), not Chrome's Views bubble.
void HostedBrowserWindow::UpdateExclusiveAccessBubble(
    const ExclusiveAccessBubbleParams& params,
    ExclusiveAccessBubbleHideCallback first_hide_callback) {
  base::WeakPtr<HostedBrowserWindow> self = weak_factory_.GetWeakPtr();
  ReportFullscreenTab();
  if (!self) {
    return;
  }
  std::u16string hint;
  if (params.has_download || params.type != EXCLUSIVE_ACCESS_BUBBLE_TYPE_NONE) {
    hint = exclusive_access_bubble::GetInstructionTextForType(
        params.type, u"Esc", std::nullopt, params.has_download,
        /*notify_overridden=*/false);
  }
  if (host_) {
    host_->ShowExclusiveAccessHint(hint);
  }
  if (first_hide_callback) {
    std::move(first_hide_callback)
        .Run(ExclusiveAccessBubbleHideReason::kNotShown);
  }
}

void HostedBrowserWindow::OnExclusiveAccessUserInput() {
  if (exclusive_access_bubble_) {
    exclusive_access_bubble_->OnUserInput();
  }
}

ExclusiveAccessManager* HostedBrowserWindow::GetExclusiveAccessManager() {
  return browser_ ? ExclusiveAccessManager::From(browser_)
                  : nullptr;
}

ui::AcceleratorProvider* HostedBrowserWindow::GetAcceleratorProvider() {
  return this;
}

bool HostedBrowserWindow::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  if (command_id != IDC_FULLSCREEN) {
    return false;
  }
  *accelerator =
      ui::Accelerator(ui::VKEY_F, ui::EF_CONTROL_DOWN | ui::EF_COMMAND_DOWN);
  return true;
}

gfx::NativeView HostedBrowserWindow::GetBubbleParentView() const {
  return host_ && host_->widget() ? host_->widget()->GetNativeView()
                                  : gfx::NativeView();
}

gfx::Rect HostedBrowserWindow::GetClientAreaBoundsInScreen() const {
  return host_ && host_->widget()
             ? host_->widget()->GetClientAreaBoundsInScreen()
             : gfx::Rect();
}

bool HostedBrowserWindow::IsImmersiveModeEnabled() const {
  return false;
}

gfx::Rect HostedBrowserWindow::GetTopContainerBoundsInScreen() {
  return gfx::Rect();
}

void HostedBrowserWindow::RepositionExclusiveAccessBubble() {
  if (exclusive_access_bubble_) {
    exclusive_access_bubble_->RepositionIfVisible();
  }
}

void HostedBrowserWindow::DestroyAnyExclusiveAccessBubble() {
  exclusive_access_bubble_.reset();
  exclusive_access_bubble_destruction_task_id_.reset();
}

void HostedBrowserWindow::ReportFullscreenTab() {
  ExclusiveAccessManager* manager =
      browser_ ? ExclusiveAccessManager::From(browser_) : nullptr;
  FullscreenController* controller =
      manager ? manager->fullscreen_controller() : nullptr;
  content::WebContents* now = controller && controller->IsTabFullscreen()
                                  ? controller->exclusive_access_tab()
                                  : nullptr;
  content::WebContents* was = fullscreen_tab_.get();
  if (now == was) {
    return;
  }
  fullscreen_tab_ = now ? now->GetWeakPtr() : nullptr;
  if (!host_ || !browser_) {
    return;
  }
  base::WeakPtr<content::WebContents> entering = fullscreen_tab_;
  if (was) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<content::WebContents> left) {
                         content::RenderWidgetHostView* view =
                             left ? left->GetRenderWidgetHostView() : nullptr;
                         if (view && !left->IsFullscreen()) {
                           view->GetRenderWidgetHost()
                               ->SynchronizeVisualProperties();
                         }
                       },
                       was->GetWeakPtr()));
    host_->FullscreenChanged(was, false);
  }
  if (content::WebContents* contents = entering.get();
      contents && contents == fullscreen_tab_.get() && host_) {
    host_->FullscreenChanged(contents, true);
  }
}

bool HostedBrowserWindow::IsExclusiveAccessBubbleDisplayed() const {
  return exclusive_access_bubble_ && (exclusive_access_bubble_->IsShowing() ||
                                      exclusive_access_bubble_->IsVisible());
}

content::WebContents* HostedBrowserWindow::GetWebContentsForExclusiveAccess() {
  return browser_ ? browser_->GetTabStripModel()->GetActiveWebContents()
                  : nullptr;
}

bool HostedBrowserWindow::CanUserEnterFullscreen() const {
  return true;
}

bool HostedBrowserWindow::CanUserExitFullscreen() const {
  return true;
}

std::string HostedBrowserWindow::GetWorkspace() const {
  return std::string();
}

bool HostedBrowserWindow::IsVisibleOnAllWorkspaces() const {
  return false;
}

void HostedBrowserWindow::ShowEmojiPanel() {
  base::WeakPtr<HostedBrowserWindow> self = weak_factory_.GetWeakPtr();
  if (content::WebContents* contents =
          browser_ ? browser_->GetTabStripModel()->GetActiveWebContents()
                   : nullptr) {
    if (!contents->ForSecurityDropFullscreen(display::kInvalidDisplayId) ||
        !self) {
      return;
    }
  }
  if (host_ && host_->widget()) {
    host_->widget()->ShowEmojiPanel();
    return;
  }
  [NSApp orderFrontCharacterPalette:nil];
}


std::unique_ptr<content::EyeDropper> HostedBrowserWindow::OpenEyeDropper(
    content::RenderFrameHost* frame,
    content::EyeDropperListener* listener) {
  return ShowEyeDropper(frame, listener);
}

bool HostedBrowserWindow::IsUnframedModeEnabled() const {
  return false;
}

bool HostedBrowserWindow::GetCanResize() {
  return true;
}

ui::mojom::WindowShowState HostedBrowserWindow::GetWindowShowState() const {
  if (IsMinimized()) {
    return ui::mojom::WindowShowState::kMinimized;
  }
  if (IsFullscreen()) {
    return ui::mojom::WindowShowState::kFullscreen;
  }
  return ui::mojom::WindowShowState::kNormal;
}

BrowserView* HostedBrowserWindow::AsBrowserView() {
  return nullptr;
}

}
