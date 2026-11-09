// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/root_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "pane/ui/design/motion.h"
#include "pane/ui/interaction_log.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

constexpr int kSidebarWidth = 260;
constexpr int kPanelInset = 8;
constexpr int kToolbarHeight = 43;
// The content panel's squircle (18), as a layer draws it: a circle of the
// radius that follows it within a fifth of a pixel (ADR 0057), so no mask
// composites the page each frame.
constexpr float kPageCornerRadius = 18 * 0.57f;

// The sheet's scrim, black in both appearances, and its card, at the
// window's right, as the profile sheet sits (Paper 05).
constexpr SkColor kScrim = SkColorSetARGB(0x40, 0, 0, 0);
constexpr int kSheetWidth = 400;
constexpr int kSheetInset = 8;

class SheetView : public views::View {
  METADATA_HEADER(SheetView, views::View)

 public:
  explicit SheetView(base::RepeatingClosure close) : close_(close) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetBackground(views::CreateSolidBackground(kScrim));
    card_ = AddChildView(std::make_unique<views::View>());
    card_->SetPaintToLayer();
    card_->layer()->SetFillsBoundsOpaquely(false);
    card_->SetBackground(
        views::CreateRoundedRectBackground(SK_ColorWHITE, 18 * 0.57f));
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

RootView::RootView(RowPressed row_pressed, AddressEntered address_entered)
    : row_pressed_(std::move(row_pressed)),
      address_entered_(std::move(address_entered)) {
  SetBackground(views::CreateSolidBackground(SkColorSetRGB(0xF4, 0xF4, 0xF5)));

  sidebar_ = AddChildView(std::make_unique<views::View>());
  sidebar_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(44, 8, 8, 8),
      2));

  panel_ = AddChildView(std::make_unique<views::View>());
  panel_->SetBackground(views::CreateSolidBackground(SK_ColorWHITE));

  address_ = AddChildView(std::make_unique<views::Textfield>());
  address_->set_controller(this);
  address_->SetAccessibleName(u"Address");

  page_ = AddChildView(std::make_unique<views::WebView>(nullptr));
}

RootView::~RootView() = default;

void RootView::SetRows(const std::vector<SidebarRow>& rows) {
  sidebar_->RemoveAllChildViews();
  for (const SidebarRow& row : rows) {
    auto button = std::make_unique<views::LabelButton>(
        base::BindRepeating(row_pressed_, row), row.title);
    button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    if (row.selected) {
      button->SetBackground(
          views::CreateRoundedRectBackground(SkColorSetARGB(0x14, 0, 0, 0), 7));
    }
    if (row.tab_id.empty()) {
      button->SetEnabledTextColors(SkColorSetRGB(0x6B, 0x6B, 0x70));
    }
    sidebar_->AddChildView(std::move(button));
  }
  sidebar_->InvalidateLayout();
}

void RootView::ShowPage(content::WebContents* contents) {
  if (page_->web_contents() == contents) {
    return;
  }
  page_->SetWebContents(contents);
  page_->holder()->SetNativeViewCornerRadii(
      gfx::RoundedCornersF(0, 0, kPageCornerRadius, kPageCornerRadius));
}

content::WebContents* RootView::page() const {
  return page_->web_contents();
}

void RootView::SetAddress(const std::u16string& address) {
  address_->SetText(address);
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
  address_entered_.Run(std::u16string(sender->GetText()));
  return true;
}

gfx::Rect RootView::PageBounds() const {
  return page_->bounds();
}

bool RootView::IsDragArea(const gfx::Point& point) {
  if (point.y() >= kPanelInset + kToolbarHeight) {
    return false;
  }
  views::View* hit = GetEventHandlerForPoint(point);
  return hit == this || hit == sidebar_ || hit == panel_;
}

void RootView::Layout(PassKey) {
  sidebar_->SetBounds(0, 0, kSidebarWidth, height());
  const gfx::Rect panel(kSidebarWidth, kPanelInset,
                        std::max(width() - kSidebarWidth - kPanelInset, 0),
                        std::max(height() - 2 * kPanelInset, 0));
  panel_->SetBoundsRect(panel);
  address_->SetBounds(panel.x() + 92, panel.y() + 8,
                      std::max(panel.width() - 184, 0), kToolbarHeight - 16);
  page_->SetBounds(panel.x(), panel.y() + kToolbarHeight, panel.width(),
                   std::max(panel.height() - kToolbarHeight, 0));
  if (sheet_) {
    sheet_->SetBoundsRect(GetLocalBounds());
  }
}

BEGIN_METADATA(RootView)
END_METADATA

}  // namespace pane
