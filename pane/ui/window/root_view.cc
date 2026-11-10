// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/root_view.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "pane/ui/after_frame.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/dialog.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/panels/app_icon_picker.h"
#include "pane/ui/panels/settings_view.h"
#include "pane/ui/view_ids.h"
#include "pane/ui/window/empty_state_view.h"
#include "pane/ui/window/panel_frame.h"
#include "pane/ui/window/quit_dialog.h"
#include "pane/ui/window/sad_tab_view.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The window's least size, as the Electron app's (ADR 0036).
constexpr gfx::Size kMinimumSize(900, 600);

// The content panel: flush with the sidebar, 8 from the window's other edges
// (m-2 ml-0), rounded at the panel's 18 (PanelFrame draws it).
constexpr int kPanelInset = 8;

// The sheet's card, at the window's right, as the profile sheet sits (Paper
// 05), over its scrim.
constexpr int kSheetWidth = 400;
constexpr int kSheetInset = 8;

class SheetView : public views::View {
  METADATA_HEADER(SheetView, views::View)

 public:
  explicit SheetView(base::RepeatingClosure close) : close_(close) {
    SetID(kViewIdProfileSheet);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetBackground(views::CreateSolidBackground(kPaneColorSheetScrim));
    card_ = AddChildView(std::make_unique<views::View>());
    card_->SetPaintToLayer();
    card_->layer()->SetFillsBoundsOpaquely(false);
    card_->SetBackground(std::make_unique<ControlBackground>(
        [] {
          ControlLook look(ControlShape::Squircle(radius::kPanel));
          look.fill = kPaneColorCard;
          return look;
        }(),
        nullptr));
    card_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(24), 12));
    field_ = card_->AddChildView(std::make_unique<views::Textfield>());
    field_->SetAccessibleName(u"Name");
    // The platform tiles interaction-timing presses, as the profile sheet's.
    for (const char16_t* platform : {u"Windows", u"macOS"}) {
      card_->AddChildView(std::make_unique<views::LabelButton>(
          base::BindRepeating(&SheetView::ChoosePlatform,
                              base::Unretained(this)),
          platform));
    }
    card_->AddChildView(std::make_unique<views::LabelButton>(close_, u"Done"));
    AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
    AddAccelerator(ui::Accelerator(ui::VKEY_RETURN, ui::EF_NONE));
  }

  void Focus() { field_->RequestFocus(); }

  void ChoosePlatform() {
    if (GetWidget()) {
      LogInteraction(Interaction::kChoosePlatform,
                     GetWidget()->GetCompositor());
    }
  }

  // The scrim fades in as the card slides in from the window's right
  // (Motion.sheet).
  void AnimateIn() {
    layer()->SetOpacity(0);
    card_->layer()->SetTransform(
        gfx::Transform::MakeTranslation(kSheetWidth + kSheetInset, 0));
    LayerMotion::Start(layer(), motion::Sheet(), {.opacity = 1.0f});
    LayerMotion::Start(card_->layer(), motion::Sheet(),
                       {.transform = gfx::Transform()});
  }

  // views::View:
  void Layout(PassKey) override {
    card_->SetBounds(width() - kSheetWidth - kSheetInset, kSheetInset,
                     kSheetWidth, std::max(height() - 2 * kSheetInset, 0));
  }

  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    close_.Run();
    return true;
  }

 private:
  base::RepeatingClosure close_;
  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<views::Textfield> field_ = nullptr;
};

BEGIN_METADATA(SheetView)
END_METADATA

}  // namespace

RootView::Callbacks::Callbacks() = default;
RootView::Callbacks::Callbacks(Callbacks&&) = default;
RootView::Callbacks& RootView::Callbacks::operator=(Callbacks&&) = default;
RootView::Callbacks::~Callbacks() = default;

RootView::Shows::Shows() = default;
RootView::Shows::Shows(const Shows&) = default;
RootView::Shows& RootView::Shows::operator=(const Shows&) = default;
RootView::Shows::~Shows() = default;

RootView::RootView(Callbacks callbacks,
                   model::ToolbarProgress* progress,
                   AppIconImages* images)
    : callbacks_(std::move(callbacks)), images_(images) {
  // The window's own layer holds this color and nothing else, so it has no
  // tiles: what draws over it draws on layers of its own, each as small as
  // what it draws (ADR 0073).
  SetBackground(views::CreateSolidBackground(kPaneColorWindowBackground));

  // The panel's card, on its shadow and in its ring, under everything.
  frame_ = AddChildView(std::make_unique<PanelFrame>());

  // The sidebar's column, and its edge over it and the panel's, under every
  // overlay that comes after.
  sidebar_ = AddChildView(
      std::make_unique<SidebarView>(std::move(callbacks_.sidebar)));

  // The toolbar's row, at the panel's top while a tab shows, over the card.
  toolbar_ = AddChildView(
      std::make_unique<ToolbarView>(std::move(callbacks_.toolbar), progress));
  toolbar_->SetPaintToLayer();
  toolbar_->layer()->SetFillsBoundsOpaquely(false);

  // The page in its card, and what shows there with no tab selected.
  page_ = AddChildView(std::make_unique<views::WebView>(nullptr));
  page_->SetID(kViewIdContent);
  page_->GetViewAccessibility().SetName(u"Web content");
  empty_ = AddChildView(std::make_unique<EmptyStateView>());
  sad_tab_ = AddChildView(
      std::make_unique<SadTabView>(std::move(callbacks_.reload_crashed)));
  // Over the card's middle, a layer the compositor fills.
  for (views::View* place : {static_cast<views::View*>(empty_.get()),
                             static_cast<views::View*>(sad_tab_.get())}) {
    place->SetPaintToLayer();
    place->layer()->SetFillsBoundsOpaquely(false);
  }
  // Settings, in the panel in the page's place: made now, so showing it
  // makes nothing.
  settings_ = AddChildView(
      std::make_unique<SettingsView>(std::move(callbacks_.settings), *images));
  resize_ = AddChildView(
      std::make_unique<ResizeHandle>(std::move(callbacks_.resize)));
  // Over the rest, under the profile sheet; made now, so the first frame
  // that shows it makes nothing.
  switcher_ = AddChildView(std::make_unique<TabSwitcherView>());
  switcher_->SetVisible(false);
  // Quit Pane?, over everything else; made now, so the first ⌘Q makes
  // nothing. It takes every key while it shows.
  ModalDialog::Callbacks quit;
  quit.dismiss = callbacks_.cancel_quit;
  quit.accept = callbacks_.quit;
  quit_dialog_ = AddChildView(std::make_unique<ModalDialog>(
      QuitDialog::kWidth,
      std::make_unique<QuitDialog>(callbacks_.cancel_quit, callbacks_.quit),
      std::move(quit), ModalDialog::Keys::kEvery));
  quit_dialog_->SetID(kViewIdQuit);
  ShowPlaces();
}

RootView::~RootView() = default;

void RootView::SetSidebar(const SidebarContent& content) {
  sidebar_->SetContent(content);
}

void RootView::SetShows(const Shows& shows) {
  const bool content_changed = shows.content != shows_.content;
  const bool laid_out_again =
      content_changed ||
      std::round(shows.sidebar_width) != std::round(shows_.sidebar_width);
  // The page's place had the keys: the sad tab takes them in it, and the
  // page back from it.
  const bool place_focused = page_->HasFocus() || sad_tab_->HasFocus();
  shows_ = shows;
  toolbar_->SetShows(shows.toolbar);
  settings_->SetShows(shows.settings);
  ShowAppIconPicker(shows.app_icon_picker);
  if (app_icon_picker_) {
    app_icon_picker_->SetSelection(shows.settings.settings.app_icon);
  }
  // The picker, made once Settings' first frame is out, so its first
  // showing makes nothing.
  if (shows.content == Content::kSettings && !app_icon_dialog_ && GetWidget()) {
    AfterFrame(GetWidget()->GetCompositor(),
               base::BindOnce(&RootView::PrepareAppIconPicker,
                              weak_factory_.GetWeakPtr()));
  }
  sidebar_->SetCanCollapseAll(shows.can_collapse_all);
  resize_->SetWidth(shows.sidebar_width);
  if (laid_out_again) {
    ShowPlaces();
  }
  if (content_changed && place_focused &&
      (shows.content == Content::kPage || shows.content == Content::kSadTab)) {
    FocusPage();
  }
}

void RootView::SetSidebarWidth(double width) {
  if (std::round(width) == std::round(shows_.sidebar_width)) {
    return;
  }
  shows_.sidebar_width = width;
  resize_->SetWidth(width);
  InvalidateLayout();
  SchedulePaint();
}

// What shows goes or comes at once, not at the next layout: a new tab's
// address field takes the keys in the same call that makes the toolbar show.
void RootView::ShowPlaces() {
  // The sidebar steps aside while a page fills the window, as the toolbar
  // does, which shows over a tab's page on the browser page.
  sidebar_->SetVisible(!page_fullscreen_);
  resize_->SetVisible(!page_fullscreen_);
  const bool tab =
      shows_.content == Content::kPage || shows_.content == Content::kSadTab;
  toolbar_->SetVisible(tab && !page_fullscreen_);
  empty_->SetVisible(shows_.content == Content::kEmpty && !page_fullscreen_);
  // The sad tab takes the page's place, on the panel's card: the page that
  // died leaves it.
  sad_tab_->SetVisible(shows_.content == Content::kSadTab && !page_fullscreen_);
  // Settings takes the panel, and the page leaves it, hidden, so it draws
  // nothing under Settings, which never masks it (ADR 0073).
  settings_->SetVisible(shows_.content == Content::kSettings &&
                        !page_fullscreen_);
  page_->SetVisible(shows_.content == Content::kPage);
  InvalidateLayout();
  SchedulePaint();
}

void RootView::SetPageFullscreen(bool fullscreen) {
  if (fullscreen == page_fullscreen_) {
    return;
  }
  page_fullscreen_ = fullscreen;
  RoundPage();
  ShowPlaces();
}

void RootView::ShowPage(content::WebContents* contents) {
  if (page_->web_contents() == contents) {
    return;
  }
  page_->SetWebContents(contents);
  RoundPage();
}

// The corners go on the page's own layer, which comes and goes with it
// (NativeViewHostMac has none to round with no page attached), and none while
// the page fills the window: the panel's, as a circle that follows the
// squircle within a fifth of a pixel (ADR 0057), so no mask composites the
// page each frame. All four, as the Swift's page layer rounds them, its top
// two under the toolbar too: the user's call over Paper's square ones, and
// 04.7 now draws them so.
void RootView::RoundPage() {
  if (!page_->web_contents()) {
    return;
  }
  page_->holder()->SetNativeViewCornerRadii(
      gfx::RoundedCornersF(page_fullscreen_ ? 0 : kPanelLayerRadius));
}

content::WebContents* RootView::page() const {
  return page_->web_contents();
}

std::u16string RootView::address() const {
  return toolbar_->address();
}

std::string RootView::focus() const {
  const views::FocusManager* manager =
      GetWidget() ? GetWidget()->GetFocusManager() : nullptr;
  const views::View* focused = manager ? manager->GetFocusedView() : nullptr;
  if (!focused) {
    return "none";
  }
  const std::string_view name = NearestViewIdName(focused);
  return std::string(name.empty() ? focused->GetClassName() : name);
}

void RootView::FocusPage() {
  if (sad_tab_->GetVisible()) {
    sad_tab_->RequestFocus();
  } else if (page_->web_contents()) {
    page_->RequestFocus();
  }
}

void RootView::FocusSettings() {
  if (settings_->GetVisible()) {
    settings_->RequestFocus();
  }
}

void RootView::FocusAddress() {
  toolbar_->FocusAddress();
}

void RootView::PrepareAppIconPicker() {
  if (app_icon_dialog_) {
    return;
  }
  auto picker = std::make_unique<AppIconPicker>(
      *images_, shows_.settings.settings.app_icon, callbacks_.choose_app_icon,
      callbacks_.close_app_icon_picker);
  app_icon_picker_ = picker.get();
  ModalDialog::Callbacks dialog;
  dialog.dismiss = callbacks_.close_app_icon_picker;
  dialog.accept = callbacks_.close_app_icon_picker;
  app_icon_dialog_ = AddChildView(std::make_unique<ModalDialog>(
      AppIconPicker::kWidth, std::move(picker), std::move(dialog)));
  app_icon_dialog_->SetID(kViewIdAppIconPicker);
  app_icon_dialog_->SetBoundsRect(GetLocalBounds());
}

AppIconPicker* RootView::app_icon_picker() {
  return app_icon_dialog_ && app_icon_dialog_->showing()
             ? app_icon_picker_.get()
             : nullptr;
}

void RootView::ShowAppIconPicker(bool show) {
  if (!show) {
    // It fades as it goes, taking nothing meanwhile; Settings has the keys
    // again.
    if (app_icon_dialog_ && app_icon_dialog_->showing()) {
      app_icon_dialog_->AnimateOut();
      FocusSettings();
    }
    return;
  }
  PrepareAppIconPicker();
  if (app_icon_dialog_->showing()) {
    return;
  }
  app_icon_picker_->SetSelection(shows_.settings.settings.app_icon);
  // Over whatever came after it.
  ReorderChildView(app_icon_dialog_, children().size());
  app_icon_dialog_->SetBoundsRect(GetLocalBounds());
  app_icon_dialog_->AnimateIn();
}

void RootView::ShowQuitDialog(bool show) {
  if (!show) {
    if (!quit_dialog_->showing()) {
      return;
    }
    // It fades as it goes, taking nothing meanwhile; the keys go back to
    // what had them, or to the page.
    quit_dialog_->AnimateOut();
    views::View* before = focus_before_quit_.view();
    focus_before_quit_.SetView(nullptr);
    if (before && before->IsDrawn() && before->IsFocusable()) {
      before->RequestFocus();
    } else {
      FocusPage();
    }
    return;
  }
  if (quit_dialog_->showing()) {
    return;
  }
  views::FocusManager* focus = GetFocusManager();
  focus_before_quit_.SetView(focus ? focus->GetFocusedView() : nullptr);
  // Over whatever came after it: the App icon picker, the switcher.
  ReorderChildView(quit_dialog_, children().size());
  quit_dialog_->SetBoundsRect(GetLocalBounds());
  quit_dialog_->AnimateIn();
}

bool RootView::quit_dialog_showing() const {
  return quit_dialog_->showing();
}

bool RootView::TakesEveryKey() const {
  return quit_dialog_->takes_every_key();
}

void RootView::ShowSheet(bool show) {
  if (show == sheet_visible()) {
    return;
  }
  if (show) {
    if (GetWidget()) {
      LogInteraction(Interaction::kOpenProfileSheet,
                     GetWidget()->GetCompositor());
    }
    auto sheet = std::make_unique<SheetView>(base::BindRepeating(
        &RootView::ShowSheet, base::Unretained(this), false));
    SheetView* view = sheet.get();
    sheet_ = AddChildView(std::move(sheet));
    sheet_->SetBoundsRect(GetLocalBounds());
    view->AnimateIn();
    view->Focus();
    return;
  }
  // Gone at once; its exit animates once G5's motion driver lands.
  std::unique_ptr<views::View> sheet = RemoveChildViewT(sheet_.get());
  sheet_ = nullptr;
  sheet.reset();
  FocusAddress();
}

bool RootView::sheet_visible() const {
  return sheet_ != nullptr;
}

void RootView::ShowSwitcher(std::vector<TabSwitcherView::Group> groups,
                            int selected) {
  switcher_->SetGroups(std::move(groups), selected);
  switcher_->SetBoundsRect(GetLocalBounds());
  switcher_->SetVisible(true);
}

void RootView::SelectInSwitcher(int selected) {
  switcher_->SetSelected(selected);
}

void RootView::HideSwitcher() {
  switcher_->SetVisible(false);
}

gfx::Rect RootView::PageBounds() const {
  return page_->bounds();
}

gfx::RoundedCornersF RootView::PageCorners() const {
  return page_->web_contents() ? page_->holder()->GetNativeViewCornerRadii()
                               : gfx::RoundedCornersF();
}

SkPath RootView::PanelPath() const {
  return SquirclePath(gfx::RectF(panel_bounds_),
                      page_fullscreen_ ? 0 : radius::kPanel);
}

bool RootView::IsDragArea(const gfx::Point& point) {
  if (page_fullscreen_) {
    return false;
  }
  views::View* hit = GetEventHandlerForPoint(point);
  // The header's background and its title; the window's own around the
  // panel, which takes its presses as the panel's card did.
  return hit == sidebar_->header() ||
         (hit == this && !PanelPath().contains(point.x(), point.y()));
}

gfx::Size RootView::GetMinimumSize() const {
  return kMinimumSize;
}

void RootView::Layout(PassKey) {
  const int sidebar_width = static_cast<int>(std::round(shows_.sidebar_width));

  sidebar_->SetBounds(0, 0, sidebar_width, height());
  resize_->SetBounds(sidebar_width - ResizeHandle::kOverSidebar, 0,
                     ResizeHandle::kWidth, height());

  panel_bounds_ =
      page_fullscreen_
          ? GetLocalBounds()
          : gfx::Rect(sidebar_width, kPanelInset,
                      std::max(width() - sidebar_width - kPanelInset, 0),
                      std::max(height() - 2 * kPanelInset, 0));
  frame_->SetBoundsRect(GetLocalBounds());
  frame_->SetPanel(panel_bounds_, page_fullscreen_);

  toolbar_->SetBounds(panel_bounds_.x(), panel_bounds_.y(),
                      panel_bounds_.width(), ToolbarView::kHeight);

  const int top = toolbar_->GetVisible() ? ToolbarView::kHeight : 0;
  page_->SetBounds(panel_bounds_.x(), panel_bounds_.y() + top,
                   panel_bounds_.width(),
                   std::max(panel_bounds_.height() - top, 0));

  empty_->SetBoundsRect(panel_bounds_);
  settings_->SetBoundsRect(panel_bounds_);
  // Where the page is, under the toolbar.
  sad_tab_->SetBoundsRect(page_->bounds());
  switcher_->SetBoundsRect(GetLocalBounds());

  if (sheet_) {
    sheet_->SetBoundsRect(GetLocalBounds());
  }
  if (app_icon_dialog_) {
    app_icon_dialog_->SetBoundsRect(GetLocalBounds());
  }
  quit_dialog_->SetBoundsRect(GetLocalBounds());
}

BEGIN_METADATA(RootView)
END_METADATA

}  // namespace pane
