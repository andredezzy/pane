// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/root_view.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/draw_looper.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/view_ids.h"
#include "pane/ui/window/empty_state_view.h"
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
// (m-2 ml-0), rounded at the panel's 18; its ring a point outside it, rounded
// a point more, and its shadow-panel, 0 2px 8px (a Gaussian of 4).
constexpr int kPanelInset = 8;
constexpr float kRingRadius = radius::kPanel + 1;
constexpr gfx::Vector2dF kShadowOffset(0, 2);
constexpr float kShadowSigma = 4;

// The page's corners, the panel's, as a layer draws them: a circle of the
// radius that follows the squircle within a fifth of a pixel (ADR 0057), so
// no mask composites the page each frame. All four, as the Swift's page layer
// rounds them, its top two under the toolbar too: the user's call over
// Paper's square ones, and 04.7 now draws them so.
constexpr float kPageCornerRadius = radius::kPanel * kCircularEquivalent;

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

RootView::RootView(Callbacks callbacks, model::ToolbarProgress* progress)
    : callbacks_(std::move(callbacks)) {
  SetBackground(views::CreateSolidBackground(kPaneColorWindowBackground));

  // The sidebar's column, and its edge over it and the panel's, under every
  // overlay that comes after.
  sidebar_ = AddChildView(
      std::make_unique<SidebarView>(std::move(callbacks_.sidebar)));

  // The toolbar's row, at the panel's top while a tab shows.
  toolbar_ = AddChildView(
      std::make_unique<ToolbarView>(std::move(callbacks_.toolbar), progress));

  // The page in its card, and what shows there with no tab selected.
  page_ = AddChildView(std::make_unique<views::WebView>(nullptr));
  page_->SetID(kViewIdContent);
  page_->GetViewAccessibility().SetName(u"Web content");
  empty_ = AddChildView(std::make_unique<EmptyStateView>());
  sad_tab_ = AddChildView(
      std::make_unique<SadTabView>(std::move(callbacks_.reload_crashed)));
  resize_ = AddChildView(
      std::make_unique<ResizeHandle>(std::move(callbacks_.resize)));
  // Over the rest, under the profile sheet; made now, so the first frame
  // that shows it makes nothing.
  switcher_ = AddChildView(std::make_unique<TabSwitcherView>());
  switcher_->SetVisible(false);
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
  // A page of the app's (Settings) takes the panel: the page leaves it, as
  // the Swift's did (ADR 0073), until Settings' surface (H6) draws there.
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
// the page fills the window.
void RootView::RoundPage() {
  if (!page_->web_contents()) {
    return;
  }
  page_->holder()->SetNativeViewCornerRadii(
      gfx::RoundedCornersF(page_fullscreen_ ? 0 : kPageCornerRadius));
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

void RootView::FocusAddress() {
  toolbar_->FocusAddress();
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

  toolbar_->SetBounds(panel_bounds_.x(), panel_bounds_.y(),
                      panel_bounds_.width(), ToolbarView::kHeight);

  const int top = toolbar_->GetVisible() ? ToolbarView::kHeight : 0;
  page_->SetBounds(panel_bounds_.x(), panel_bounds_.y() + top,
                   panel_bounds_.width(),
                   std::max(panel_bounds_.height() - top, 0));

  empty_->SetBoundsRect(panel_bounds_);
  // Where the page is, under the toolbar.
  sad_tab_->SetBoundsRect(page_->bounds());
  switcher_->SetBoundsRect(GetLocalBounds());

  if (sheet_) {
    sheet_->SetBoundsRect(GetLocalBounds());
  }
}

// The window's fill, then the content panel's card on its shadow, with its
// ring: shadow-panel's 1-pixel ring and soft drop (Elevation.swift), both
// outside the card as CSS's box-shadows are. A page filling the window has
// the card square and bare.
void RootView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors || panel_bounds_.IsEmpty()) {
    return;
  }
  const gfx::RectF card(panel_bounds_);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(colors->GetColor(kPaneColorCard));
  if (page_fullscreen_) {
    canvas->DrawRect(card, flags);
    return;
  }
  cc::DrawLooperBuilder shadow;
  shadow.AddUnmodifiedContent();
  shadow.AddShadow(
      {kShadowOffset.x(), kShadowOffset.y()}, kShadowSigma,
      SkColor4f::FromColor(colors->GetColor(kPaneColorPanelShadow)),
      cc::DrawLooper::kOverrideAlphaFlag);
  flags.setLooper(shadow.Detach());
  canvas->DrawPath(SquirclePath(card, radius::kPanel), flags);

  cc::PaintFlags ring;
  ring.setAntiAlias(true);
  ring.setStyle(cc::PaintFlags::kStroke_Style);
  ring.setStrokeWidth(1);
  ring.setColor(colors->GetColor(kPaneColorBorder));
  gfx::RectF outside = card;
  outside.Outset(1);
  canvas->DrawPath(SquircleStrokePath(outside, kRingRadius, 1), ring);
}

BEGIN_METADATA(RootView)
END_METADATA

}  // namespace pane
