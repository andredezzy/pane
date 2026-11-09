// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_ROOT_VIEW_H_
#define PANE_UI_WINDOW_ROOT_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class SkPath;

namespace content {
class WebContents;
}  // namespace content

namespace views {
class Label;
class Textfield;
class WebView;
}  // namespace views

namespace pane {

class EmptyStateView;
class IconButton;

// What the sidebar lists: each profile, then its tabs.
struct SidebarRow {
  std::string profile_id;
  // Empty for the profile's own row.
  std::string tab_id;
  std::u16string title;
  // A tab's: the one shown. A profile's: the profile shown.
  bool selected = false;
  // A profile's row: the profile's color, its mark's.
  SkColor color = SK_ColorTRANSPARENT;
};

// Everything a Pane window draws (Paper's main window, the Swift's RootView),
// as the Electron app laid it out: the sidebar's column on the left, 260
// points wide unless resized, and the content panel flush beside it, inset 8
// from the window's other edges, rounded 18, on its ring and shadow. The
// panel holds the toolbar's row and, under it, the selected tab's page in
// its card; with no tab selected, the empty state. A page's fullscreen fills
// the window, the sidebar and the toolbar stepping aside (ADR 0025). The
// window moves by its background, the sidebar's header included (ADR 0036).
class RootView : public views::View, public views::TextfieldController {
  METADATA_HEADER(RootView, views::View)

 public:
  using RowPressed = base::RepeatingCallback<void(const SidebarRow&)>;
  // Return in the address field, with what it holds.
  using AddressEntered = base::RepeatingCallback<void(const std::u16string&)>;

  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    RowPressed row_pressed;
    AddressEntered address_entered;
    base::RepeatingClosure back;
    base::RepeatingClosure collapse_all;
  };

  // What the content panel shows: the browser page with a tab selected,
  // with none (the empty state), or a page of the app's (Settings).
  enum class Content { kPage, kEmpty, kOther };

  // The rest of what the window shows, from the app.
  struct Shows {
    Content content = Content::kEmpty;
    // The settings' width, in points (ADR 0061).
    double sidebar_width = 260;
    // The shown page has somewhere to go back to.
    bool can_go_back = false;
    // Some profile is expanded.
    bool can_collapse_all = false;
  };

  explicit RootView(Callbacks callbacks);
  RootView(const RootView&) = delete;
  RootView& operator=(const RootView&) = delete;
  ~RootView() override;

  void SetRows(const std::vector<SidebarRow>& rows);
  void SetShows(const Shows& shows);
  // The page fills the window, or leaves it.
  void SetPageFullscreen(bool fullscreen);
  bool page_fullscreen() const { return page_fullscreen_; }

  void ShowPage(content::WebContents* contents);
  content::WebContents* page() const;
  void SetAddress(const std::u16string& address);
  void FocusAddress();
  void FocusPage();
  std::u16string address() const;
  // What has the keyboard, for the channel: the focused view's id, or the
  // nearest one's around it ("pane.toolbar.address", "pane.content"), else
  // its class; "none" when nothing has it.
  std::string focus() const;
  // The prototype's sheet over the page (G4): a scrim and a card with a
  // field, keys into it and not the page, Esc and Done closing it.
  void ShowSheet(bool show);
  bool sheet_visible() const;
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
  void OnPaint(gfx::Canvas* canvas) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  std::unique_ptr<views::View> MakeRow(const SidebarRow& row);
  // The content panel's shape, where the window draws its card.
  SkPath PanelPath() const;
  // The page's corners, as its layer rounds them.
  void RoundPage();
  // The sidebar, the toolbar, and the empty state shown or hidden for what
  // the window shows.
  void ShowPlaces();

  Callbacks callbacks_;
  Shows shows_;
  bool page_fullscreen_ = false;
  // Where the content panel is, as last laid out.
  gfx::Rect panel_bounds_;
  // Each row's profile and tab, in the sidebar's order.
  std::vector<std::string> row_keys_;
  raw_ptr<views::View> sidebar_ = nullptr;
  raw_ptr<views::View> header_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<IconButton> collapse_all_ = nullptr;
  raw_ptr<views::View> list_ = nullptr;
  raw_ptr<views::View> toolbar_ = nullptr;
  raw_ptr<IconButton> back_ = nullptr;
  raw_ptr<views::Textfield> address_ = nullptr;
  raw_ptr<views::WebView> page_ = nullptr;
  raw_ptr<EmptyStateView> empty_ = nullptr;
  raw_ptr<views::View> sheet_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_ROOT_VIEW_H_
