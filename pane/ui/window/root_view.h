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
#include "pane/ui/panels/settings_view.h"
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
class ModalDialog;
class PageDialog;
class PanelFrame;
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
  // bar's Quit: Quit Pane? (and the PIN screen, L1's).
  bool TakesEveryKey() const;
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
  // Esc or Return, ahead of what has the window's keys: the page's dialog
  // showing takes it, unless a dialog over the whole window shows over it.
  // Whether it took it.
  bool PageDialogKey(const ui::KeyEvent& event);
  // The prototype's sheet over the page (G4): a scrim and a card with a
  // field, keys into it and not the page, Esc and Done closing it.
  void ShowSheet(bool show);
  bool sheet_visible() const;
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

 private:
  // The content panel's shape, where the window draws its card.
  SkPath PanelPath() const;
  // The page's corners, as its layer rounds them.
  void RoundPage();
  // The sidebar, the toolbar, and the empty state shown or hidden for what
  // the window shows.
  void ShowPlaces();
  // The App icon picker comes over the window, or goes; made hidden ahead
  // of its first showing, and kept.
  void ShowAppIconPicker(bool show);
  void PrepareAppIconPicker();
  // Places the page's dialogs, the one showing and those on their way out.
  void PlacePageDialogs();
  // One gone: its card leaves the window.
  void PageDialogLeft(DialogCard* card);

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
  raw_ptr<EmptyStateView> empty_ = nullptr;
  raw_ptr<SadTabView> sad_tab_ = nullptr;
  raw_ptr<TabSwitcherView> switcher_ = nullptr;
  raw_ptr<views::View> sheet_ = nullptr;
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
  // Hands a page's dialog its Esc and Return ahead of the focused view.
  std::unique_ptr<ui::EventHandler> page_dialog_keys_;
  base::WeakPtrFactory<RootView> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_ROOT_VIEW_H_
