// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/root_view.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "cc/paint/draw_looper.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/icon_button.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/view_ids.h"
#include "pane/ui/window/empty_state_view.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The window's least size, as the Electron app's (ADR 0036).
constexpr gfx::Size kMinimumSize(900, 600);

// The sidebar's header: the traffic lights' row, 39 points, its title 82 in,
// after them, and Collapse all, a 28-point circle, 8 from the column's edge,
// as the rows below. The title is centered a point above the row's bottom,
// on the lights' line (ADR 0036).
constexpr int kHeaderHeight = 39;
constexpr int kHeaderLeading = 82;
constexpr int kHeaderTrailing = 8;
constexpr int kHeaderBottom = 1;
constexpr int kCollapseAllSize = 28;
constexpr TextStyle kTitle{12, weight::kSemibold, 0};

// The list under it: 8 in from the column's sides, 4 from its ends, its rows
// 4 apart.
constexpr int kListInset = 8;
constexpr int kListEnds = 4;
constexpr int kRowSpacing = 4;

// The content panel: flush with the sidebar, 8 from the window's other edges
// (m-2 ml-0), rounded at the panel's 18; its ring a point outside it, rounded
// a point more, and its shadow-panel, 0 2px 8px (a Gaussian of 4).
constexpr int kPanelInset = 8;
constexpr float kRingRadius = radius::kPanel + 1;
constexpr gfx::Vector2dF kShadowOffset(0, 2);
constexpr float kShadowSigma = 4;

// The toolbar's row: the page starts 43 points down the panel, a pixel under
// the toolbar's 42 (the Electron app's TOOLBAR_HEIGHT less its gutter). Its
// buttons, 32-point circles, start 5 in.
constexpr int kToolbarHeight = 43;
constexpr int kToolbarLeading = 5;
constexpr int kToolbarButtonSize = 32;

// The page's corners, the panel's, as a layer draws them: a circle of the
// radius that follows the squircle within a fifth of a pixel (ADR 0057), so
// no mask composites the page each frame. All four, as the Swift's page layer
// rounds them, its top two under the toolbar too (Paper's 04.7 has those
// square: the user's call).
constexpr float kPageCornerRadius = radius::kPanel * kCircularEquivalent;

// The sheet's card, at the window's right, as the profile sheet sits (Paper
// 05), over its scrim.
constexpr int kSheetWidth = 400;
constexpr int kSheetInset = 8;

// A profile's name: 12 points, medium (ADR 0060).
constexpr TextStyle kProfileName{12, weight::kMedium, 16};

// A profile's mark, in its color: a bar 4 points wide while another profile
// is shown, growing into a 16-point square while it's the one shown, on the
// spring an indicator's own change of shape takes (Motion.morph), as the
// Swift sidebar's ProfileHeaderContent.
class ProfileMark : public views::View {
  METADATA_HEADER(ProfileMark, views::View)

 public:
  ProfileMark(SkColor color, bool active)
      : color_(color),
        active_(active),
        morph_(active ? 1 : 0,
               base::BindRepeating(&ProfileMark::Morphed,
                                   base::Unretained(this))) {}

  void SetActive(bool active) {
    if (active == active_) {
      return;
    }
    active_ = active;
    morph_.MoveTo(active ? 1 : 0, motion::Morph(),
                  GetWidget() ? GetWidget()->GetCompositor() : nullptr);
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(std::ceil(Width()), kSize);
  }

  // A squircle whose radius grows with it, 2 to 4 (ADR 0064).
  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color_);
    canvas->DrawPath(
        SquirclePath(gfx::RectF(0, 0, Width(), kSize), 2 + 2 * morph_.value()),
        flags);
  }

 private:
  static constexpr int kSize = 16;

  // 4 points at 0, 16 at 1, a spring's overshoot past it.
  float Width() const { return 4 + 12 * morph_.value(); }

  void Morphed(double value) {
    PreferredSizeChanged();
    SchedulePaint();
  }

  const SkColor color_;
  bool active_;
  ValueMotion morph_;
};

BEGIN_METADATA(ProfileMark)
END_METADATA

// A profile's row: its mark and its name, 36 points tall.
class ProfileRow : public views::Button {
  METADATA_HEADER(ProfileRow, views::Button)

 public:
  ProfileRow(PressedCallback pressed, const SidebarRow& row)
      : views::Button(std::move(pressed)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 8), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    mark_ =
        AddChildView(std::make_unique<ProfileMark>(row.color, row.selected));
    label_ =
        AddChildView(MakeLabel(u"", kProfileName, kPaneColorMutedForeground));
    label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    layout->SetFlexForView(label_, 1);
    Update(row);
  }

  void Update(const SidebarRow& row) {
    if (label_->GetText() != row.title) {
      label_->SetText(row.title);
      GetViewAccessibility().SetName(row.title);
    }
    label_->SetEnabledColor(row.selected ? kPaneColorAccentForeground
                                         : kPaneColorMutedForeground);
    mark_->SetActive(row.selected);
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(
        views::Button::CalculatePreferredSize(available_size).width(), 36);
  }

 private:
  raw_ptr<ProfileMark> mark_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
};

BEGIN_METADATA(ProfileRow)
END_METADATA

std::string KeyOf(const SidebarRow& row) {
  return row.profile_id + "/" + row.tab_id;
}

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

RootView::RootView(Callbacks callbacks) : callbacks_(std::move(callbacks)) {
  SetBackground(views::CreateSolidBackground(kPaneColorWindowBackground));

  // The sidebar's column: its header, the lights' row, then its list.
  sidebar_ = AddChildView(std::make_unique<views::View>());
  sidebar_->SetID(kViewIdSidebar);
  sidebar_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  sidebar_->GetViewAccessibility().SetName(u"Sidebar");
  header_ = sidebar_->AddChildView(std::make_unique<views::View>());
  header_->SetID(kViewIdSidebarHeader);
  title_ = header_->AddChildView(
      MakeLabel(u"Pane", kTitle, kPaneColorMutedForeground));
  // The title moves the window, as the rest of the header does.
  title_->SetCanProcessEventsWithinSubtree(false);
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  collapse_all_ = header_->AddChildView(std::make_unique<IconButton>(
      callbacks_.collapse_all, kCollapseAllSize,
      IconButton::Glyph{Icon::kChevronsDownUp, 16}, u"Collapse all profiles"));
  collapse_all_->SetID(kViewIdCollapseAll);
  collapse_all_->SetEnabled(false);
  list_ = sidebar_->AddChildView(std::make_unique<views::View>());
  list_->SetID(kViewIdSidebarProfiles);
  list_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kListEnds, kListInset), kRowSpacing));

  // The toolbar's row, at the panel's top while a tab shows.
  toolbar_ = AddChildView(std::make_unique<views::View>());
  toolbar_->SetID(kViewIdToolbar);
  back_ = toolbar_->AddChildView(std::make_unique<IconButton>(
      callbacks_.back, kToolbarButtonSize,
      IconButton::Glyph{Icon::kChevronLeft, 24, /*stroke_width=*/1.5f},
      u"Back"));
  back_->SetID(kViewIdBack);
  back_->SetEnabled(false);
  address_ = toolbar_->AddChildView(std::make_unique<views::Textfield>());
  address_->SetID(kViewIdAddress);
  address_->set_controller(this);
  address_->SetAccessibleName(u"Address");

  // The page in its card, and what shows there with no tab selected.
  page_ = AddChildView(std::make_unique<views::WebView>(nullptr));
  page_->SetID(kViewIdContent);
  page_->GetViewAccessibility().SetName(u"Web content");
  empty_ = AddChildView(std::make_unique<EmptyStateView>());
  ShowPlaces();
}

RootView::~RootView() = default;

void RootView::SetRows(const std::vector<SidebarRow>& rows) {
  std::vector<std::string> keys;
  for (const SidebarRow& row : rows) {
    keys.push_back(KeyOf(row));
  }
  // Rows came, went, or moved: each keeps its view, found by its profile and
  // tab, so what moves keeps moving (a profile's mark, mid-morph).
  if (keys != row_keys_) {
    std::map<std::string, std::unique_ptr<views::View>> kept;
    for (const std::string& key : row_keys_) {
      kept[key] = list_->RemoveChildViewT(list_->children().front());
    }
    for (size_t i = 0; i < rows.size(); ++i) {
      std::unique_ptr<views::View>& view = kept[keys[i]];
      list_->AddChildView(view ? std::move(view) : MakeRow(rows[i]));
    }
    row_keys_ = std::move(keys);
    list_->InvalidateLayout();
  }
  for (size_t i = 0; i < rows.size(); ++i) {
    views::View* view = list_->children()[i];
    const SidebarRow& row = rows[i];
    if (row.tab_id.empty()) {
      static_cast<ProfileRow*>(view)->Update(row);
      continue;
    }
    // A selection moves at once (DESIGN.md).
    auto* button = static_cast<views::LabelButton*>(view);
    if (button->GetText() != row.title) {
      button->SetText(row.title);
    }
    if (row.selected != !!button->GetBackground()) {
      button->SetBackground(row.selected ? std::make_unique<ControlBackground>(
                                               RowLook(true), nullptr)
                                         : nullptr);
    }
  }
}

std::unique_ptr<views::View> RootView::MakeRow(const SidebarRow& row) {
  if (row.tab_id.empty()) {
    return std::make_unique<ProfileRow>(
        base::BindRepeating(callbacks_.row_pressed, row), row);
  }
  auto button = std::make_unique<views::LabelButton>(
      base::BindRepeating(callbacks_.row_pressed, row), row.title);
  button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return button;
}

void RootView::SetShows(const Shows& shows) {
  const bool laid_out_again =
      shows.content != shows_.content ||
      std::round(shows.sidebar_width) != std::round(shows_.sidebar_width);
  shows_ = shows;
  back_->SetEnabled(shows.can_go_back);
  collapse_all_->SetEnabled(shows.can_collapse_all);
  if (laid_out_again) {
    ShowPlaces();
  }
}

// What shows goes or comes at once, not at the next layout: a new tab's
// address field takes the keys in the same call that makes the toolbar show.
void RootView::ShowPlaces() {
  // The sidebar steps aside while a page fills the window, as the toolbar
  // does, which shows over a tab's page on the browser page.
  sidebar_->SetVisible(!page_fullscreen_);
  toolbar_->SetVisible(shows_.content == Content::kPage && !page_fullscreen_);
  empty_->SetVisible(shows_.content == Content::kEmpty && !page_fullscreen_);
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

void RootView::SetAddress(const std::u16string& address) {
  address_->SetText(address);
}

std::u16string RootView::address() const {
  return std::u16string(address_->GetText());
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
  if (page_->web_contents()) {
    page_->RequestFocus();
  }
}

void RootView::FocusAddress() {
  address_->RequestFocus();
  address_->SelectAll(false);
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

bool RootView::HandleKeyEvent(views::Textfield* sender,
                              const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed ||
      key_event.key_code() != ui::VKEY_RETURN) {
    return false;
  }
  callbacks_.address_entered.Run(std::u16string(sender->GetText()));
  return true;
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
  return hit == header_ ||
         (hit == this && !PanelPath().contains(point.x(), point.y()));
}

gfx::Size RootView::GetMinimumSize() const {
  return kMinimumSize;
}

void RootView::Layout(PassKey) {
  const int sidebar_width = static_cast<int>(std::round(shows_.sidebar_width));

  sidebar_->SetBounds(0, 0, sidebar_width, height());
  header_->SetBounds(0, 0, sidebar_width, kHeaderHeight);
  const int header_row = kHeaderHeight - kHeaderBottom;
  collapse_all_->SetBounds(
      std::max(sidebar_width - kHeaderTrailing - kCollapseAllSize, 0),
      (header_row - kCollapseAllSize) / 2, kCollapseAllSize, kCollapseAllSize);
  title_->SetBounds(kHeaderLeading, 0,
                    std::max(collapse_all_->x() - 8 - kHeaderLeading, 0),
                    header_row);
  list_->SetBounds(0, kHeaderHeight, sidebar_width,
                   std::max(height() - kHeaderHeight, 0));

  panel_bounds_ =
      page_fullscreen_
          ? GetLocalBounds()
          : gfx::Rect(sidebar_width, kPanelInset,
                      std::max(width() - sidebar_width - kPanelInset, 0),
                      std::max(height() - 2 * kPanelInset, 0));

  toolbar_->SetBounds(panel_bounds_.x(), panel_bounds_.y(),
                      panel_bounds_.width(), kToolbarHeight);
  const int button_top = (kToolbarHeight - 1 - kToolbarButtonSize) / 2;
  back_->SetBounds(kToolbarLeading, button_top, kToolbarButtonSize,
                   kToolbarButtonSize);
  address_->SetBounds(92, 8, std::max(panel_bounds_.width() - 184, 0),
                      kToolbarHeight - 16);

  const int top = toolbar_->GetVisible() ? kToolbarHeight : 0;
  page_->SetBounds(panel_bounds_.x(), panel_bounds_.y() + top,
                   panel_bounds_.width(),
                   std::max(panel_bounds_.height() - top, 0));

  empty_->SetBoundsRect(panel_bounds_);

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
