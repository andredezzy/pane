// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_ROOT_VIEW_H_
#define PANE_UI_WINDOW_ROOT_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "pane/model/app_settings.h"
#include "pane/ui/extensions/extensions_menu_view.h"
#include "pane/ui/lock/lock_screen_view.h"
#include "pane/ui/page/find_bar_view.h"
#include "pane/ui/panels/settings_view.h"
#include "pane/ui/sheets/profile_sheet.h"
#include "pane/ui/sidebar/resize_handle.h"
#include "pane/ui/sidebar/sidebar_view.h"
#include "pane/ui/tabs/tab_switcher_view.h"
#include "pane/ui/toolbar/toolbar_view.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"

class SkPath;

namespace content {
class WebContents;
}  // namespace content

namespace ui {
class EventHandler;
class KeyEvent;
}  // namespace ui

namespace views {
class WebView;
}  // namespace views

namespace pane {

namespace model {
class ToolbarProgress;
}

class AppIconImages;
class AppIconPicker;
class DialogCard;
class EmptyStateView;
class ExtensionPromptOverlay;
class ExtensionPromptView;
class ExtensionsUi;
class ExclusiveAccessHint;
struct ExtensionPrompt;
class ModalDialog;
class PageDialog;
class PanelFrame;
class ProfileSheetOverlay;
class SadTabView;

// Everything a Pane window draws (Paper's main window, the Swift's RootView),
// as the Electron app laid it out: the sidebar's column on the left, 260
// points wide unless resized, and the content panel flush beside it, inset 8
// from the window's other edges, rounded 18, on its ring and shadow. The
// panel holds the toolbar's row and, under it, the selected tab's page in
// its card; with no tab selected, the empty state. A page's fullscreen fills
// the window, the sidebar and the toolbar stepping aside (ADR 0025). The
// window moves by its background, the sidebar's header included (ADR 0036).
class RootView : public views::View {
  METADATA_HEADER(RootView, views::View)

 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    SidebarView::Callbacks sidebar;
    ResizeHandle::Callbacks resize;
    ToolbarView::Callbacks toolbar;
    // The sad tab's Reload, and Return on it.
    base::RepeatingClosure reload_crashed;
    SettingsView::Callbacks settings;
    // The App icon picker's sky, and its Done, Return, Esc, or a click
    // outside it.
    base::RepeatingCallback<void(model::AppIcon)> choose_app_icon;
    base::RepeatingClosure close_app_icon_picker;
    // Quit Pane?'s Cancel (Esc, a click outside it) and Quit (Return).
    base::RepeatingClosure cancel_quit;
    base::RepeatingClosure quit;
    // Find in page's bar: what is typed, a step, and close (also a press
    // anywhere else in the window).
    FindBarView::Callbacks find_bar;
    // The window's extensions, the app's, which outlive the view: the
    // toolbar's actions and puzzle, and the menus they open.
    raw_ptr<ExtensionsUi> extensions = nullptr;
    // The profile sheet's Create or Save, and its closing.
    ProfileSheet::Callbacks profile_sheet;
    // The PIN screen's keys and presses, and its update's action.
    LockScreenView::Callbacks lock;
  };

  // What the content panel shows: the browser page with a tab selected,
  // the sad tab in that tab's place (a crash loop, or a profile that would
  // not open), with none selected (the empty state), or Settings, in the
  // page's place, which leaves the panel (ADR 0073).
  enum class Content { kPage, kSadTab, kEmpty, kSettings };

  // The rest of what the window shows, from the app.
  struct Shows {
    Shows();
    Shows(const Shows&);
    Shows& operator=(const Shows&);
    ~Shows();

    Content content = Content::kEmpty;
    // The sidebar's width, in points (ADR 0061): the settings', or a drag's.
    double sidebar_width = 260;
    // Some profile is expanded.
    bool can_collapse_all = false;
    // The selected tab's toolbar.
    ToolbarView::Shows toolbar;
    // Settings' values, and whether the App icon picker shows over the
    // window.
    SettingsView::Shows settings;
    bool app_icon_picker = false;
    // The PIN screen over everything, or the data's move on its panel.
    LockScreenView::Shows lock;
  };

  // `progress` and `images`, the app's, outlive the view.
  RootView(Callbacks callbacks,
           model::ToolbarProgress* progress,
           AppIconImages* images);
  RootView(const RootView&) = delete;
  RootView& operator=(const RootView&) = delete;
  ~RootView() override;

  void SetSidebar(const SidebarContent& content);
  void SetShows(const Shows& shows);
  // The sidebar at `width`, as a drag of its edge lays it out, step by step.
  void SetSidebarWidth(double width);
  SidebarView* sidebar() { return sidebar_; }
  ToolbarView* toolbar() { return toolbar_; }
  SettingsView* settings() { return settings_; }
  // While it shows.
  AppIconPicker* app_icon_picker();
  // The page fills the window, or leaves it.
  void SetPageFullscreen(bool fullscreen);
  bool page_fullscreen() const { return page_fullscreen_; }

  void ShowPage(content::WebContents* contents);
  content::WebContents* page() const;
  // DevTools docked to the page shown came, went, or moved their splitter:
  // they fill the page's place, and the page sits over them where their
  // splitter puts it (ADR 0024), its corners rounded where they meet the
  // panel's.
  void UpdateDevTools();
  // Find in page's bar (ADR 0067): shown as `state` has it, over the window
  // at its top right, while the browser page shows, with a shield under it
  // over the rest of the window, which a press closes it from and goes
  // nowhere else (the Swift's FindOverlay). It comes and goes at once.
  void ShowFind(const FindBarView::State& state);
  const FindBarView::State& find_state() const { return find_bar_->state(); }
  // Its field takes the keys, its text selected, while it shows.
  void FocusFind();
  bool FindHasFocus() const;
  // While it shows: the bar, and its card in this view, its ring included.
  FindBarView* find_bar();
  gfx::Rect FindBarBounds() const;
  // The extensions menu under the toolbar's puzzle, its trailing edge on the
  // puzzle's and 4 under it, and an extension's more menu beside it, level
  // with its row, both at once (the Swift showed them with no transition);
  // over a page's dialog and under the switcher, on a shield over the rest
  // of the window that a press closes them from and that goes nowhere else.
  void ShowExtensionsMenu(const ExtensionsMenuView::Shows& shows);
  void HideExtensionsMenu();
  // While they show.
  ExtensionsMenuView* extensions_menu();
  ExtensionMoreMenuView* extension_more_menu();
  // Adding or removing an extension asks over the window (ADR 0056): over a
  // page's dialog and the extensions menu, under the switcher and what shows
  // over the whole window, at once, as the Swift's showed it. Removing hangs
  // from the extension's pinned button, else the puzzle, while the toolbar
  // shows. It takes the keys while it shows, and gives them back to what had
  // them as it goes, at once.
  void ShowExtensionPrompt(const ExtensionPrompt& prompt,
                           const gfx::ImageSkia& app_icon);
  void HideExtensionPrompt();
  // While one asks: its view, and its card in this view, its edge included.
  ExtensionPromptView* extension_prompt();
  gfx::Rect ExtensionPromptBounds() const;
  // The full screen's Esc hint (ADR 0025), or none for an empty one.
  void ShowExclusiveAccessHint(const std::u16string& hint);
  std::u16string exclusive_access_hint() const;
  void FocusAddress();
  // The page takes the keys, or the sad tab in its place.
  void FocusPage();
  // Settings takes the keys, in the page's place.
  void FocusSettings();
  std::u16string address() const;
  // What has the keyboard, for the channel: the focused view's id, or the
  // nearest one's around it ("pane.toolbar.address", "pane.content"), else
  // its class; "none" when nothing has it.
  std::string focus() const;
  // Quit Pane? over the window (ADR 0053), fading in as its card grows and
  // taking the keys; gone, the keys go back to what had them. Made with the
  // view, so its first showing makes nothing.
  void ShowQuitDialog(bool show);
  // Shown, and not on its way out.
  bool quit_dialog_showing() const;
  // An overlay takes every key, the window's commands' included but the menu
  // bar's Quit: Quit Pane?, and the PIN screen.
  bool TakesEveryKey() const;
  // The PIN screen over the window (ADR 0042): rising over it as it comes
  // and sinking as it goes, in place at the window's first frame, over every
  // other overlay but the full screen's hint. It takes the keys while a PIN
  // shows, and gives them back to what had them, else to the page.
  LockScreenView* lock_screen() { return lock_; }
  // A wrong PIN: its dots shake.
  void ShakePin();
  // A page's dialog over the window (PageDialogs, ADR 0021), under the tab
  // switcher and what shows over the whole window, on the alert's card: in
  // the window's middle, its top at three eighths of the window's height,
  // raised to fit, as the Swift's AlertPlacement put it. It fades in as it
  // grows from 0.97 (Motion.panel) and takes the keys; the page under it
  // takes no press, as the sidebar and the toolbar still do, as with
  // Chrome's tab-modal dialogs. Gone, it answers no more and fades as it
  // shrinks, and the keys go back to what had them, else to the page.
  void ShowPageDialog(std::unique_ptr<PageDialog> dialog);
  void HidePageDialog();
  // The one showing, not one on its way out.
  PageDialog* page_dialog();
  // Esc or Return, ahead of what has the window's keys: the extensions menu
  // takes Esc, its more menu first (the Swift's KeyRouting), then the page's
  // dialog showing takes either, unless what shows over them (the switcher,
  // a dialog over the whole window) has the keys. Whether one took it.
  bool OverlayKey(const ui::KeyEvent& event);
  // The profile sheet over the window (ADR 0120): the form for a new
  // profile, or for `profile` as it is. Its scrim fades in as its panel
  // slides in from the window's right (Motion.sheet), and its name field
  // takes the keys; Esc closes it, Return creates. Made hidden once the
  // window's first frame with the profiles is out (PrepareProfileSheet), so
  // its first showing makes nothing.
  void ShowProfileSheet(const model::BrowserProfile* profile);
  // It goes the way it came, and what had the keys before has them again, or
  // the page.
  void HideProfileSheet();
  void PrepareProfileSheet();
  // While it shows, not on its way out.
  ProfileSheet* profile_sheet();
  // Its panel, its ring included, in this view.
  gfx::Rect ProfileSheetBounds() const;
  // Runs `rested` once the sheet stops sliding in, at once if it is still,
  // and never if it goes meanwhile.
  void WhenProfileSheetRests(base::OnceClosure rested);
  // The tab switcher over the window, at once, its selection on the tab at
  // `selected`; moved, at once; gone, at once.
  void ShowSwitcher(std::vector<TabSwitcherView::Group> groups, int selected);
  void SelectInSwitcher(int selected);
  void HideSwitcher();
  // While it shows.
  TabSwitcherView* switcher() { return switcher_; }
  // Where the page is, in this view, and its layer's corners.
  gfx::Rect PageBounds() const;
  gfx::RoundedCornersF PageCorners() const;
  // Whether a press at `point` moves the window: the background under no
  // control, as the Electron app's drag regions had it (the gutters around
  // the panel, and the sidebar's header, its title included), never the
  // panel, a control, the sidebar's list, or the page.
  bool IsDragArea(const gfx::Point& point);

  // views::View:
  gfx::Size GetMinimumSize() const override;
  void Layout(PassKey) override;
  // Esc while the extensions menu shows and nothing has the keys: the
  // window's accelerator for it, which the handler ahead of a focused view
  // can't hear then.
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  // A layer showing, hiding, coming, or going changes what a screen reader
  // reaches (UpdateAccessibleLayers).
  void ChildVisibilityChanged(views::View* child) override;
  void ViewHierarchyChanged(
      const views::ViewHierarchyChangedDetails& details) override;

 private:
  // A screen reader reaches the layers that show, and none under the PIN
  // screen, a sheet, or a dialog that shows (UpdateAccessibleLayers).
  void UpdateScreenReaderLayers();
  // The content panel's shape, where the window draws its card.
  SkPath PanelPath() const;
  // The page's corners, and docked DevTools', as their layers round them.
  void RoundPage();
  // The find bar shows or hides for what the window shows.
  void ShowFindPlaces();
  // The sidebar, the toolbar, and the empty state shown or hidden for what
  // the window shows.
  void ShowPlaces();
  // The App icon picker comes over the window, or goes; made hidden ahead
  // of its first showing, and kept.
  void ShowAppIconPicker(bool show);
  // The PIN screen shows what `shows` holds, taking the keys or giving them
  // back.
  void ShowLock(const LockScreenView::Shows& shows);
  // An overlay going gives the keys back to `before`, what had them as it
  // came, else to the page; never while the PIN screen holds them, as at a
  // Lock, which dismisses what showed (UiApp::LockNow): it gives them back
  // itself as it goes (ShowLock).
  void ReturnKeys(views::View* before);
  // The PIN screen, and the full screen's hint over it, over everything else.
  void RaiseTopOverlays();
  void PrepareAppIconPicker();
  // Places the page's dialogs, the one showing and those on their way out.
  void PlacePageDialogs();
  // The one showing has come in, unless another took its place.
  void PageDialogEntered(DialogCard* card);
  // One gone: its card leaves the window.
  void PageDialogLeft(DialogCard* card);
  // The extensions menus under the puzzle, where the toolbar puts it.
  void PlaceExtensionsMenu();
  // An extension's prompt, the removal's under its anchor.
  void PlaceExtensionPrompt();
  // A prompt gone: it leaves the window once the press that answered it is
  // through.
  void ExtensionPromptLeft(ExtensionPromptOverlay* overlay);

  Callbacks callbacks_;
  Shows shows_;
  bool page_fullscreen_ = false;
  // Where the content panel is, as last laid out.
  gfx::Rect panel_bounds_;
  raw_ptr<PanelFrame> frame_ = nullptr;
  raw_ptr<SidebarView> sidebar_ = nullptr;
  raw_ptr<ResizeHandle> resize_ = nullptr;
  raw_ptr<ToolbarView> toolbar_ = nullptr;
  raw_ptr<views::WebView> page_ = nullptr;
  // DevTools docked to the page, under it.
  raw_ptr<views::WebView> devtools_ = nullptr;
  raw_ptr<EmptyStateView> empty_ = nullptr;
  raw_ptr<SadTabView> sad_tab_ = nullptr;
  raw_ptr<TabSwitcherView> switcher_ = nullptr;
  // The profile sheet over the window, once made, and what had the keys
  // before it showed.
  raw_ptr<ProfileSheetOverlay> profile_sheet_ = nullptr;
  views::ViewTracker focus_before_sheet_;
  const raw_ptr<AppIconImages> images_;
  raw_ptr<SettingsView> settings_ = nullptr;
  // The picker's dialog over the window, and its content, once made.
  raw_ptr<ModalDialog> app_icon_dialog_ = nullptr;
  raw_ptr<AppIconPicker> app_icon_picker_ = nullptr;
  // Quit Pane?'s dialog, and what had the keys before it showed.
  raw_ptr<ModalDialog> quit_dialog_ = nullptr;
  views::ViewTracker focus_before_quit_;
  // A page's dialog showing, whether it is on its way in, those on their
  // way out, what took presses meant for the page under it, and what had
  // the keys before the first showed.
  raw_ptr<DialogCard> page_dialog_ = nullptr;
  bool page_dialog_entering_ = false;
  std::vector<raw_ptr<DialogCard>> page_dialogs_leaving_;
  raw_ptr<views::View> page_shield_ = nullptr;
  views::ViewTracker focus_before_page_dialog_;
  // Find in page's bar on its card, the shield under it, and what it shows.
  raw_ptr<views::View> find_shield_ = nullptr;
  raw_ptr<DialogCard> find_card_ = nullptr;
  raw_ptr<FindBarView> find_bar_ = nullptr;
  // The extensions menu and the more menu on their cards, and the shield
  // under them.
  raw_ptr<views::View> extensions_shield_ = nullptr;
  raw_ptr<DialogCard> extensions_card_ = nullptr;
  raw_ptr<ExtensionsMenuView> extensions_menu_ = nullptr;
  raw_ptr<DialogCard> more_card_ = nullptr;
  raw_ptr<ExtensionMoreMenuView> more_menu_ = nullptr;
  // An extension's prompt over the window, while one asks, and what had the
  // keys before it.
  raw_ptr<ExtensionPromptOverlay> extension_prompt_ = nullptr;
  views::ViewTracker focus_before_prompt_;
  // The PIN screen, over everything but the hint, and what had the keys
  // before it took them.
  raw_ptr<LockScreenView> lock_ = nullptr;
  views::ViewTracker focus_before_lock_;
  // The window's first frame has left: what comes after moves.
  bool first_frame_requested_ = false;
  bool first_frame_out_ = false;
  // The full screen's Esc hint, over everything.
  raw_ptr<ExclusiveAccessHint> hint_ = nullptr;
  // Hands the extensions menu its Esc, and a page's dialog its Esc and
  // Return, ahead of the focused view.
  std::unique_ptr<ui::EventHandler> page_dialog_keys_;
  base::WeakPtrFactory<RootView> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_ROOT_VIEW_H_
