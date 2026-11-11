// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/extensions/extensions_ui.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/command.h"
#include "extensions/common/extension.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/extensions/extension_shortcuts.h"
#include "pane/browser/ui/window_host.h"
#include "pane/model/extension_text.h"
#include "pane/ui/app/commands.h"
#include "pane/ui/extensions/action_menu.h"
#include "pane/ui/extensions/action_popup.h"
#include "pane/ui/extensions/browser_extensions.h"
#include "pane/ui/extensions/extension_actions_view.h"
#include "pane/ui/extensions/extensions_menu_view.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/toolbar/toolbar_view.h"
#include "pane/ui/window/root_view.h"
#include "pane/ui/window/window.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The Chrome Web Store's extensions, and an extension's own page there.
constexpr char kWebStoreURL[] =
    "https://chromewebstore.google.com/category/extensions";
constexpr char kWebStoreDetailURL[] =
    "https://chromewebstore.google.com/detail/";

// How soon after a click outside the popup closed it a press on its own
// button counts as that click: the press then closes it, as a second click
// on a toolbar button closes its popup in Chrome, rather than opening it
// again (the Swift's NSPopover could close and open again).
constexpr base::TimeDelta kClickClosesWithin = base::Milliseconds(500);

// The fallback where no toolbar shows the button (Settings): the window's
// top right, as the Swift's popover pointed there.
constexpr int kFallbackRight = 48;
constexpr int kFallbackTop = 8;
constexpr int kFallbackSize = 26;

}  // namespace

ExtensionsUi::ExtensionsUi(Delegate& delegate) : delegate_(delegate) {}

ExtensionsUi::~ExtensionsUi() {
  // A prompt still asking is answered no.
  if (prompt_ && prompt_->answer) {
    std::move(prompt_->answer).Run(false, false);
  }
  action_menu_runner_.reset();
  action_menu_.reset();
  popup_.reset();
  closing_.clear();
}

std::unique_ptr<HostedExtensionsContainer> ExtensionsUi::MakeContainer(
    Browser& browser) {
  return std::make_unique<BrowserExtensions>(browser, GetWeakPtr());
}

// MARK: The toolbar

void ExtensionsUi::PressPuzzle() {
  if (!delegate_->ExtensionsAllowed()) {
    return;
  }
  if (menu_open_) {
    CloseMenu();
    return;
  }
  if (!delegate_->ShownProfile()) {
    return;
  }
  LogInteraction(Interaction::kOpenExtensionsMenu,
                 delegate_->ExtensionsWindow().compositor());
  // What the profile holds now, so the menu opens on it in this frame.
  BrowserExtensions* shown = Shown();
  shown_ = shown ? shown->List() : std::vector<InstalledExtension>();
  SetMenuOpen(true);
}

void ExtensionsUi::PressAction(const std::string& extension_id) {
  // The click outside the popup that closed it was this press: it stays
  // closed.
  if (closed_by_click_ && closed_by_click_->first == extension_id &&
      base::TimeTicks::Now() - closed_by_click_->second < kClickClosesWithin) {
    closed_by_click_.reset();
    return;
  }
  RunAction(extension_id);
}

void ExtensionsUi::ShowActionMenu(const std::string& extension_id,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source) {
  views::Widget* widget = delegate_->ExtensionsWindow().widget();
  if (!widget || !delegate_->ExtensionsAllowed() ||
      (action_menu_runner_ && action_menu_runner_->IsRunning())) {
    return;
  }
  ActionMenu* menu = MakeActionMenu(extension_id);
  if (!menu) {
    return;
  }
  action_menu_extension_ = extension_id;
  // The button stays lit under its menu, as Aside's 1Password button does.
  ShowState();
  action_menu_runner_ = std::make_unique<views::MenuRunner>(
      &menu->model(), views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(&ExtensionsUi::ActionMenuGone,
                          weak_factory_.GetWeakPtr()));
  action_menu_runner_->RunMenuAt(widget, nullptr, gfx::Rect(point, gfx::Size()),
                                 views::MenuAnchorPosition::kTopLeft, source);
}

void ExtensionsUi::ActionMenuGone() {
  action_menu_extension_.reset();
  ShowState();
}

// MARK: The menu

void ExtensionsUi::PressManage() {
  CloseMenu();
  if (Profile* profile = delegate_->ShownProfile();
      profile && delegate_->ExtensionsAllowed()) {
    delegate_->ShowExtensionsPage(*profile, std::string());
  }
}

void ExtensionsUi::PressWebStore() {
  CloseMenu();
  if (Profile* profile = delegate_->ShownProfile();
      profile && delegate_->ExtensionsAllowed()) {
    delegate_->OpenExtensionTab(*profile, GURL(kWebStoreURL));
  }
}

void ExtensionsUi::PressRow(const std::string& extension_id) {
  CloseMenu();
  RunAction(extension_id);
}

void ExtensionsUi::PressPin(const std::string& extension_id) {
  Profile* profile = delegate_->ShownProfile();
  if (!profile || !delegate_->ExtensionsAllowed()) {
    return;
  }
  more_menu_.reset();
  const ToolbarActionsModel* toolbar = ToolbarActionsModel::Get(profile);
  SetPinned(*profile, extension_id,
            !(toolbar && toolbar->IsActionPinned(extension_id)));
}

void ExtensionsUi::PressMore(const std::string& extension_id) {
  if (!delegate_->ExtensionsAllowed() || !menu_open_) {
    return;
  }
  if (more_menu_ == extension_id) {
    more_menu_.reset();
  } else {
    more_menu_ = extension_id;
  }
  ShowState();
}

void ExtensionsUi::PressStorePage(const std::string& extension_id) {
  CloseMenu();
  if (Profile* profile = delegate_->ShownProfile();
      profile && delegate_->ExtensionsAllowed()) {
    delegate_->OpenExtensionTab(*profile,
                                GURL(kWebStoreDetailURL + extension_id));
  }
}

void ExtensionsUi::PressRemove(const std::string& extension_id) {
  CloseMenu();
  if (Profile* profile = delegate_->ShownProfile();
      profile && delegate_->ExtensionsAllowed()) {
    ConfirmUninstall(*profile, extension_id);
  }
}

void ExtensionsUi::PressMorePin(const std::string& extension_id) {
  PressPin(extension_id);
}

void ExtensionsUi::PressManageExtension(const std::string& extension_id) {
  CloseMenu();
  if (Profile* profile = delegate_->ShownProfile();
      profile && delegate_->ExtensionsAllowed()) {
    delegate_->ShowExtensionsPage(*profile, extension_id);
  }
}

void ExtensionsUi::PressOptions(const std::string& extension_id) {
  CloseMenu();
  Profile* profile = delegate_->ShownProfile();
  if (!profile || !delegate_->ExtensionsAllowed()) {
    return;
  }
  for (const InstalledExtension& extension : shown_) {
    if (extension.id == extension_id && extension.options_url.is_valid()) {
      delegate_->OpenExtensionTab(*profile, extension.options_url);
    }
  }
}

void ExtensionsUi::CloseMenu() {
  if (!menu_open_ && !more_menu_) {
    return;
  }
  more_menu_.reset();
  SetMenuOpen(false);
}

bool ExtensionsUi::Escape() {
  if (more_menu_) {
    more_menu_.reset();
    ShowState();
    return true;
  }
  if (menu_open_) {
    CloseMenu();
    return true;
  }
  return false;
}

void ExtensionsUi::SetMenuOpen(bool open) {
  menu_open_ = open;
  if (!open) {
    more_menu_.reset();
  }
  ShowState();
}

// MARK: Actions and popups

void ExtensionsUi::RunAction(const std::string& extension_id) {
  Trace("PaneBrowserContext.runAction", extension_id);
  BrowserExtensions* shown = Shown();
  const extensions::Extension* extension = EnabledExtension(extension_id);
  content::WebContents* contents = shown ? shown->ActiveTab() : nullptr;
  if (!delegate_->ExtensionsAllowed() || !extension || !contents) {
    return;
  }
  // A second press on the button closes its popup, as Chrome's does.
  if (popup_ && popup_->extension_id() == extension_id &&
      popup_->browser() == &shown->browser()) {
    ClosePopupNow(/*animate=*/true);
    return;
  }
  auto* runner = extensions::ExtensionActionRunner::GetForWebContents(contents);
  if (!runner) {
    return;
  }
  // As Chrome's ExtensionActionViewModel::ExecuteUserAction: the page's
  // blocked scripts run, activeTab is granted, then the popup shows, or
  // chrome.action.onClicked is sent.
  const uint64_t interval =
      BeginInteractionInterval(Interaction::kShowExtensionPopup);
  if (runner->RunAction(extension, /*grant_tab_permissions=*/true) ==
      extensions::ExtensionAction::ShowAction::kShowPopup) {
    ShowPopup(*shown, *extension, interval, ShowPopupCallback());
    return;
  }
  // No popup: the interval ends on the frame it would have shown in.
  EndInteractionInterval(Interaction::kShowExtensionPopup, interval);
}

bool ExtensionsUi::ShowPopupForAPI(BrowserExtensions& container,
                                   const std::string& extension_id,
                                   ShowPopupCallback callback) {
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(container.browser().GetProfile())
          ->enabled_extensions()
          .GetByID(extension_id);
  if (!extension || !delegate_->ExtensionsAllowed() || Shown() != &container) {
    return false;
  }
  ShowPopup(container, *extension,
            BeginInteractionInterval(Interaction::kShowExtensionPopup),
            std::move(callback));
  return true;
}

void ExtensionsUi::ShowPopup(BrowserExtensions& container,
                             const extensions::Extension& extension,
                             std::optional<uint64_t> interval,
                             ShowPopupCallback callback) {
  Window& window = delegate_->ExtensionsWindow();
  views::Widget* widget = window.widget();
  content::WebContents* contents = container.ActiveTab();
  extensions::ExtensionAction* action =
      extensions::ExtensionActionManager::Get(container.browser().GetProfile())
          ->GetExtensionAction(extension);
  const int tab_id = contents
                         ? sessions::SessionTabHelper::IdForTab(contents).id()
                         : extensions::ExtensionAction::kDefaultTabId;
  const GURL url = action ? action->GetPopupUrl(tab_id) : GURL();
  if (!widget || !url.is_valid()) {
    if (interval) {
      EndInteractionInterval(Interaction::kShowExtensionPopup, *interval);
    }
    if (callback) {
      std::move(callback).Run(nullptr);
    }
    return;
  }
  // One popup at a time: a new one takes the place of the one showing.
  ClosePopupNow(/*animate=*/false);
  closed_by_click_.reset();
  CloseMenu();
  ActionPopup::Callbacks callbacks;
  callbacks.closed = base::BindRepeating(&ExtensionsUi::PopupClosed,
                                         weak_factory_.GetWeakPtr());
  callbacks.gone =
      base::BindRepeating(&ExtensionsUi::PopupGone, weak_factory_.GetWeakPtr());
  popup_ = std::make_unique<ActionPopup>(
      container.browser(), extension, url, *widget, PopupAnchor(extension.id()),
      /*activate=*/true, interval, std::move(callback), std::move(callbacks));
  ShowState();
}

void ExtensionsUi::ClosePopup() {
  ClosePopupNow(/*animate=*/true);
}

void ExtensionsUi::ClosePopupNow(bool animate) {
  if (!popup_) {
    return;
  }
  std::unique_ptr<ActionPopup> popup = std::move(popup_);
  if (animate) {
    popup->FadeOut();
    closing_.push_back(std::move(popup));
  } else {
    Trace("ExtensionPopup.close", popup->extension_id());
  }
  ShowState();
}

void ExtensionsUi::PopupClosed(ActionPopup* popup, bool by_click) {
  if (popup != popup_.get()) {
    return;
  }
  if (by_click) {
    closed_by_click_ = {popup->extension_id(), base::TimeTicks::Now()};
  }
  ClosePopupNow(/*animate=*/true);
}

void ExtensionsUi::PopupGone(ActionPopup* popup) {
  std::erase_if(closing_, [popup](const std::unique_ptr<ActionPopup>& each) {
    return each.get() == popup;
  });
}

gfx::Rect ExtensionsUi::PopupAnchor(const std::string& extension_id) const {
  Window& window = delegate_->ExtensionsWindow();
  RootView* root = window.root();
  if (root && root->toolbar()->GetVisible()) {
    ExtensionActionsView* actions = root->toolbar()->extension_actions();
    views::View* anchor = actions->ActionButtonFor(extension_id);
    if (!anchor || !anchor->IsDrawn()) {
      anchor = actions->puzzle();
    }
    if (anchor && anchor->IsDrawn()) {
      return anchor->GetBoundsInScreen();
    }
  }
  views::Widget* widget = window.widget();
  const gfx::Rect bounds =
      widget ? widget->GetClientAreaBoundsInScreen() : gfx::Rect();
  return gfx::Rect(bounds.right() - kFallbackRight, bounds.y() + kFallbackTop,
                   kFallbackSize, kFallbackSize);
}

void ExtensionsUi::ClosePopupOf(Browser& browser) {
  if (popup_ && popup_->browser() == &browser) {
    ClosePopupNow(/*animate=*/true);
  }
}

void ExtensionsUi::CloseMenuOf(Browser& browser) {
  if (BrowserExtensions* shown = Shown();
      shown && &shown->browser() == &browser) {
    CloseMenu();
  }
}

void ExtensionsUi::ToggleMenuOf(Browser& browser) {
  if (BrowserExtensions* shown = Shown();
      shown && &shown->browser() == &browser) {
    PressPuzzle();
  }
}

void ExtensionsUi::SetPinned(Profile& profile,
                             const std::string& extension_id,
                             bool pin) {
  PANE_TRACE(
      "call", "PaneBrowserContext.setExtensionPinned",
      base::DictValue().Set("extension", extension_id).Set("pinned", pin));
  ToolbarActionsModel* toolbar = ToolbarActionsModel::Get(&profile);
  if (!toolbar || !toolbar->HasAction(extension_id) ||
      toolbar->IsActionPinned(extension_id) == pin) {
    return;
  }
  toolbar->SetActionVisibility(extension_id, pin);
  // The pin, at once, rather than once the model's report comes.
  Refresh();
}

ActionMenu* ExtensionsUi::MakeActionMenu(const std::string& extension_id) {
  BrowserExtensions* shown = Shown();
  const extensions::Extension* extension = EnabledExtension(extension_id);
  content::WebContents* contents = shown ? shown->ActiveTab() : nullptr;
  if (!extension || !contents) {
    return nullptr;
  }
  if (action_menu_runner_ && action_menu_runner_->IsRunning()) {
    action_menu_runner_->Cancel();
  }
  action_menu_runner_.reset();
  const base::WeakPtr<ExtensionsUi> ui = weak_factory_.GetWeakPtr();
  ActionMenu::Callbacks callbacks;
  callbacks.store_page = base::BindRepeating(
      [](base::WeakPtr<ExtensionsUi> ui, std::string id) {
        if (ui) {
          ui->PressStorePage(id);
        }
      },
      ui, extension_id);
  callbacks.remove = base::BindRepeating(
      [](base::WeakPtr<ExtensionsUi> ui, std::string id) {
        if (ui) {
          ui->PressRemove(id);
        }
      },
      ui, extension_id);
  // In the profile shown, as the Swift's togglePinned: the menu goes with
  // the profile it was made for.
  callbacks.unpin = base::BindRepeating(
      [](base::WeakPtr<ExtensionsUi> ui, std::string id) {
        Profile* shown = ui ? ui->delegate_->ShownProfile() : nullptr;
        if (shown && ui->delegate_->ExtensionsAllowed()) {
          ui->SetPinned(*shown, id, false);
        }
      },
      ui, extension_id);
  callbacks.manage = base::BindRepeating(
      [](base::WeakPtr<ExtensionsUi> ui, std::string id) {
        if (ui) {
          ui->PressManageExtension(id);
        }
      },
      ui, extension_id);
  action_menu_ =
      std::make_unique<ActionMenu>(*extension, *contents, std::move(callbacks));
  return action_menu_.get();
}

// MARK: The window and the engine

void ExtensionsUi::Refresh() {
  BrowserExtensions* shown = Shown();
  shown_ = shown ? shown->List() : std::vector<InstalledExtension>();
  // The more menu of an extension that went closes.
  if (more_menu_ &&
      std::ranges::none_of(shown_, [&](const InstalledExtension& extension) {
        return extension.id == *more_menu_ && extension.enabled;
      })) {
    more_menu_.reset();
  }
  ShowState();
}

void ExtensionsUi::Lock() {
  if (action_menu_runner_ && action_menu_runner_->IsRunning()) {
    action_menu_runner_->Cancel();
  }
  CloseMenu();
  ClosePopupNow(/*animate=*/false);
  closing_.clear();
  // Nothing is added or removed behind the Lock: a prompt asking is
  // answered no.
  AnswerPrompt(false);
}

// MARK: Prompts (ADR 0056)

void ExtensionsUi::Ask(ExtensionPrompt prompt) {
  RootView* root = delegate_->ExtensionsWindow().root();
  if (!delegate_->ExtensionsAllowed() || !root) {
    if (prompt.answer) {
      std::move(prompt.answer).Run(false, false);
    }
    return;
  }
  // From the ask to the frame that shows it.
  LogInteraction(Interaction::kShowExtensionPrompt,
                 delegate_->ExtensionsWindow().compositor());
  PANE_TRACE("event", "ExtensionsController.present",
             base::DictValue()
                 .Set("kind", ExtensionPromptKindName(prompt.kind))
                 .Set("extension", prompt.extension_id));
  CloseMenu();
  std::optional<ExtensionPrompt> previous =
      std::exchange(prompt_, std::move(prompt));
  root->ShowExtensionPrompt(*prompt_, delegate_->AppIcon());
  // The one it took the place of answers no.
  if (previous && previous->answer) {
    std::move(previous->answer).Run(false, false);
  }
}

void ExtensionsUi::ConfirmUninstall(Profile& profile,
                                    const std::string& extension_id) {
  PANE_TRACE("call", "ExtensionsController.confirmUninstall",
             base::DictValue().Set("extension", extension_id));
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(&profile)->GetInstalledExtension(
          extension_id);
  if (!extension) {
    return;
  }
  ExtensionPrompt prompt;
  prompt.kind = ExtensionPrompt::Kind::kRemove;
  prompt.extension_id = extension_id;
  prompt.title =
      base::UTF8ToUTF16(model::ExtensionRemoveTitle(extension->name()));
  // Its icon, as its button shows it.
  for (const InstalledExtension& shown : shown_) {
    if (shown.id == extension_id) {
      prompt.icon = shown.icon;
    }
  }
  prompt.accept_title = u"Remove";
  prompt.cancel_title = u"Cancel";
  prompt.answer = base::BindOnce(
      [](base::WeakPtr<ExtensionsUi> ui, base::WeakPtr<Profile> profile,
         std::string id, bool accepted, bool report_abuse) {
        if (!accepted || !ui || !profile) {
          return;
        }
        ui->Uninstall(*profile, id);
        if (report_abuse) {
          ui->delegate_->OpenExtensionTab(*profile, model::ReportAbuseURL(id));
        }
      },
      weak_factory_.GetWeakPtr(), profile.GetWeakPtr(), extension_id);
  Ask(std::move(prompt));
}

void ExtensionsUi::AnswerPrompt(bool accepted) {
  if (!prompt_) {
    return;
  }
  RootView* root = delegate_->ExtensionsWindow().root();
  ExtensionPromptView* view = root ? root->extension_prompt() : nullptr;
  const bool report_abuse = accepted && view && view->report_abuse();
  ExtensionPrompt prompt = std::move(*prompt_);
  prompt_.reset();
  if (root) {
    root->HideExtensionPrompt();
  }
  if (prompt.answer) {
    std::move(prompt.answer).Run(accepted, report_abuse);
  }
}

void ExtensionsUi::AppIconChanged() {
  RootView* root = prompt_ ? delegate_->ExtensionsWindow().root() : nullptr;
  if (ExtensionPromptView* view = root ? root->extension_prompt() : nullptr) {
    view->SetAppIcon(delegate_->AppIcon());
  }
}

void ExtensionsUi::Uninstall(Profile& profile,
                             const std::string& extension_id) {
  PANE_TRACE("call", "PaneBrowserContext.uninstallExtension",
             base::DictValue().Set("extension", extension_id));
  if (!extensions::ExtensionRegistry::Get(&profile)->GetInstalledExtension(
          extension_id)) {
    return;
  }
  std::u16string error;
  extensions::ExtensionRegistrar::Get(&profile)->UninstallExtension(
      extension_id, extensions::UNINSTALL_REASON_USER_INITIATED, &error);
}

// MARK: Shortcuts (ADR 0098)

bool ExtensionsUi::RunShortcut(const ui::Accelerator& accelerator) {
  // Extensions take chords alone, and the menu bar's keys stay Pane's.
  if (!(accelerator.modifiers() &
        (ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN)) ||
      CommandForAccelerator(accelerator) || !delegate_->ExtensionsAllowed()) {
    return false;
  }
  BrowserExtensions* shown = Shown();
  if (!shown || shown->shortcuts().shortcut_handling_suspended()) {
    return false;
  }
  extensions::ExtensionId extension;
  std::string command;
  if (!shown->shortcuts().Find(accelerator, &extension, &command)) {
    return false;
  }
  PANE_TRACE(
      "call", "PaneBrowserContext.runExtensionShortcut",
      base::DictValue().Set("extension", extension).Set("command", command));
  // The action's runs as a press of its button: its popup, at its button
  // or the puzzle, or onClicked.
  if (extensions::Command::IsActionRelatedCommand(command)) {
    RunAction(extension);
    return true;
  }
  return shown->shortcuts().Notify(accelerator);
}

bool ExtensionsUi::ShortcutsSuspended() const {
  BrowserExtensions* shown = Shown();
  return shown && shown->shortcuts().shortcut_handling_suspended();
}

void ExtensionsUi::ShowState() {
  RootView* root = delegate_->ExtensionsWindow().root();
  if (!root) {
    return;
  }
  BrowserExtensions* shown = Shown();
  ExtensionActionsView::Shows actions;
  for (const InstalledExtension& extension : shown_) {
    if (extension.enabled && extension.has_action && extension.pinned) {
      actions.pinned.push_back(extension);
    }
  }
  if (popup_ && shown && popup_->browser() == &shown->browser()) {
    actions.popup = popup_->extension_id();
  }
  actions.menu_action = action_menu_extension_;
  actions.menu_open = menu_open_;
  root->toolbar()->extension_actions()->SetShows(actions);
  if (!menu_open_) {
    root->HideExtensionsMenu();
    return;
  }
  ExtensionsMenuView::Shows menu;
  for (const InstalledExtension& extension : shown_) {
    if (extension.enabled) {
      menu.extensions.push_back(extension);
    }
  }
  menu.more = more_menu_;
  root->ShowExtensionsMenu(menu);
}

void ExtensionsUi::Register(BrowserExtensions* container) {
  containers_.insert(container);
}

void ExtensionsUi::Unregister(BrowserExtensions* container) {
  containers_.erase(container);
  Browser* browser = &container->browser();
  // Its popup goes first, before the Browser it holds (ADR 0031).
  if (popup_ && popup_->browser() == browser) {
    PANE_TRACE("engine", "ExtensionPopup.closeWithBrowser",
               base::DictValue().Set("popups", 1));
    ClosePopupNow(/*animate=*/false);
  }
  std::erase_if(closing_, [browser](const std::unique_ptr<ActionPopup>& each) {
    return each->browser() == browser;
  });
}

void ExtensionsUi::ExtensionsChanged(BrowserExtensions& container) {
  if (Shown() == &container) {
    Refresh();
  }
}

void ExtensionsUi::ExtensionUnloaded(Browser& browser,
                                     const std::string& extension_id) {
  if (popup_ && popup_->browser() == &browser &&
      popup_->extension_id() == extension_id) {
    ClosePopupNow(/*animate=*/false);
  }
  std::erase_if(closing_, [&](const std::unique_ptr<ActionPopup>& each) {
    return each->browser() == &browser && each->extension_id() == extension_id;
  });
}

void ExtensionsUi::ActiveTabChanged(Browser& browser) {
  ClosePopupOf(browser);
}

BrowserExtensions* ExtensionsUi::Shown() const {
  Profile* profile = delegate_->ShownProfile();
  if (!profile) {
    return nullptr;
  }
  Window& window = delegate_->ExtensionsWindow();
  WindowHost* host = window.host();
  Browser* browser = host ? host->ExistingBrowserFor(profile) : nullptr;
  for (BrowserExtensions* container : containers_) {
    if (&container->browser() == browser) {
      return container;
    }
  }
  return nullptr;
}

const extensions::Extension* ExtensionsUi::EnabledExtension(
    const std::string& extension_id) const {
  Profile* profile = delegate_->ShownProfile();
  return profile ? extensions::ExtensionRegistry::Get(profile)
                       ->enabled_extensions()
                       .GetByID(extension_id)
                 : nullptr;
}

void ExtensionsUi::Trace(std::string_view name,
                         const std::string& extension_id) const {
  PANE_TRACE("call", name, base::DictValue().Set("extension", extension_id));
}

}  // namespace pane
