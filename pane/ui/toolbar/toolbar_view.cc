// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/toolbar/toolbar_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/icon_button.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/text.h"
#include "pane/ui/extensions/extension_actions_view.h"
#include "pane/ui/toolbar/address_field.h"
#include "pane/ui/view_ids.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/label.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The row: 42 points over the panel's 43 (the page a point under it), its
// controls centered in it; 5 in from the panel's leading edge and 6 from its
// trailing one, 4 apart, and the navigation buttons 2 apart among themselves.
constexpr int kRowHeight = ToolbarView::kHeight - 1;
constexpr int kLeading = 5;
constexpr int kTrailing = 6;
constexpr int kSpacing = 4;
constexpr int kNavigationSpacing = 2;
constexpr int kButtonSize = 32;

// Aside's chevrons: 1.5 points of stroke at 24. Its reload at 17 with the
// same 1.5 points (2.1 of the 24-unit grid), and Stop in its place.
constexpr IconButton::Glyph kBack{Icon::kChevronLeft, 24, 1.5f};
constexpr IconButton::Glyph kForward{Icon::kChevronRight, 24, 1.5f};
constexpr IconButton::Glyph kReload{Icon::kRotateCw, 17, 2.1f};
constexpr IconButton::Glyph kStop{Icon::kX, 17, 2.1f};

// The chip: 28 points, its name 12 in on each side (11 inside its 1-point
// border, as CSS's px-[11px]).
constexpr int kChipHeight = 28;
constexpr int kChipInset = 12;

}  // namespace

// The profile's name in its color: a 28-point capsule at 12 medium, as
// Aside's toolbar button, with an 8% fill and a 30% border of the color
// (the Swift's ProfileChip).
class ToolbarView::ProfileChip : public views::View {
  METADATA_HEADER(ProfileChip, views::View)

 public:
  ProfileChip() {
    SetID(kViewIdProfileChip);
    label_ = AddChildView(
        MakeLabel(std::u16string(), text::kButtonSmall, SK_ColorTRANSPARENT));
    label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }

  void Set(const std::u16string& name, SkColor color) {
    if (name == label_->GetText() && color == color_) {
      return;
    }
    color_ = color;
    label_->SetText(name);
    label_->SetEnabledColor(color);
    PreferredSizeChanged();
    SchedulePaint();
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(label_->GetPreferredSize().width() + 2 * kChipInset,
                     kChipHeight);
  }

  void Layout(PassKey) override {
    const int line = text::kButtonSmall.line_height;
    label_->SetBounds(kChipInset, (height() - line) / 2,
                      std::max(width() - 2 * kChipInset, 0), line);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::RectF bounds(GetLocalBounds());
    const ControlShape shape = ControlShape::Capsule();
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(SkColorSetA(color_, AlphaOf(0.08f)));
    canvas->DrawPath(shape.PathIn(bounds), fill);
    cc::PaintFlags border;
    border.setAntiAlias(true);
    border.setStyle(cc::PaintFlags::kStroke_Style);
    border.setStrokeWidth(1);
    border.setColor(SkColorSetA(color_, AlphaOf(0.3f)));
    canvas->DrawPath(shape.StrokePathIn(bounds, 1), border);
  }

 private:
  raw_ptr<views::Label> label_ = nullptr;
  SkColor color_ = SK_ColorTRANSPARENT;
};

BEGIN_METADATA(ToolbarView, ProfileChip)
END_METADATA

ToolbarView::Callbacks::Callbacks() = default;
ToolbarView::Callbacks::Callbacks(Callbacks&&) = default;
ToolbarView::Callbacks& ToolbarView::Callbacks::operator=(Callbacks&&) =
    default;
ToolbarView::Callbacks::~Callbacks() = default;

ToolbarView::Shows::Shows() = default;
ToolbarView::Shows::Shows(const Shows&) = default;
ToolbarView::Shows& ToolbarView::Shows::operator=(const Shows&) = default;
ToolbarView::Shows::~Shows() = default;

ToolbarView::ToolbarView(Callbacks callbacks, model::ToolbarProgress* progress)
    : callbacks_(std::move(callbacks)) {
  SetID(kViewIdToolbar);
  back_ = AddChildView(std::make_unique<IconButton>(
      callbacks_.back, kButtonSize, kBack, u"Back"));
  back_->SetID(kViewIdBack);
  back_->SetEnabled(false);
  forward_ = AddChildView(std::make_unique<IconButton>(
      callbacks_.forward, kButtonSize, kForward, u"Forward"));
  forward_->SetID(kViewIdForward);
  forward_->SetEnabled(false);
  // Reload, or Stop while the page loads: one press, what it does then.
  reload_ = AddChildView(std::make_unique<IconButton>(
      base::BindRepeating(
          [](ToolbarView* toolbar) {
            (toolbar->shows_.loading ? toolbar->callbacks_.stop
                                     : toolbar->callbacks_.reload)
                .Run();
          },
          base::Unretained(this)),
      kButtonSize, kReload, u"Reload"));
  reload_->SetID(kViewIdReload);

  address_ = AddChildView(std::make_unique<AddressField>(progress));
  address_->set_controller(this);

  actions_ = AddChildView(
      std::make_unique<ExtensionActionsView>(*callbacks_.extensions));
  chip_ = AddChildView(std::make_unique<ProfileChip>());
  chip_->SetVisible(false);
}

ToolbarView::~ToolbarView() {
  address_->set_controller(nullptr);
}

void ToolbarView::SetShows(const Shows& shows) {
  back_->SetEnabled(shows.can_go_back);
  forward_->SetEnabled(shows.can_go_forward);
  if (shows.loading != shows_.loading) {
    reload_->SetGlyph(shows.loading ? kStop : kReload);
    reload_->SetName(shows.loading ? u"Stop" : u"Reload");
  }

  // Another tab shows its own address, unless ⌘T showed it already, and what
  // has been typed since stays. The same tab's new address replaces the text
  // unless the user is typing over it.
  if (shows.tab_id != address_tab_id_) {
    address_tab_id_ = shows.tab_id;
    address_url_ = shows.url;
    address_->ShowAddress(shows.url);
  } else if (shows.url != address_url_) {
    address_url_ = shows.url;
    if (!address_->HasFocus() || !address_->IsTypedOver()) {
      address_->ShowAddress(shows.url);
    }
  }
  address_->SetTitle(shows.title);

  const bool profile_changed = shows.profile_name != shows_.profile_name ||
                               shows.profile_color != shows_.profile_color;
  shows_ = shows;
  if (profile_changed) {
    chip_->SetVisible(shows.profile_name.has_value());
    actions_->SetVisible(shows.profile_name.has_value());
    if (shows.profile_name) {
      chip_->Set(*shows.profile_name, shows.profile_color);
    }
    InvalidateLayout();
  }
}

void ToolbarView::FocusAddress() {
  address_->RequestFocus();
  address_->SelectAll(false);
}

std::u16string ToolbarView::address() const {
  return std::u16string(address_->GetText());
}

std::vector<std::pair<std::string, const views::View*>>
ToolbarView::NamedViews() const {
  std::vector<std::pair<std::string, const views::View*>> named = {
      {"pane.toolbar", this},
      {"pane.toolbar.back", back_},
      {"pane.toolbar.forward", forward_},
      {"pane.toolbar.reload", reload_},
      {"pane.toolbar.address", address_},
      {"pane.toolbar.profile", chip_},
  };
  for (auto& entry : actions_->NamedViews()) {
    named.push_back(std::move(entry));
  }
  return named;
}

void ToolbarView::Layout(PassKey) {
  const int button_top = (kRowHeight - kButtonSize) / 2;
  int x = kLeading;
  for (IconButton* button : {back_.get(), forward_.get(), reload_.get()}) {
    button->SetBounds(x, button_top, kButtonSize, kButtonSize);
    x += kButtonSize + kNavigationSpacing;
  }
  const int address_x = reload_->bounds().right() + kSpacing;

  // From the trailing edge: the chip, then X1's actions, each 4 apart.
  int right = width() - kTrailing;
  if (chip_->GetVisible()) {
    const int chip_width = chip_->GetPreferredSize().width();
    chip_->SetBounds(right - chip_width, (kRowHeight - kChipHeight) / 2,
                     chip_width, kChipHeight);
    right = chip_->x() - kSpacing;
  }
  if (actions_->GetVisible()) {
    const gfx::Size actions = actions_->GetPreferredSize();
    actions_->SetBounds(right - actions.width(),
                        (kRowHeight - actions.height()) / 2, actions.width(),
                        actions.height());
    right = actions_->x() - kSpacing;
  }
  address_->SetBounds(address_x, (kRowHeight - AddressField::kHeight) / 2,
                      std::max(right - address_x, 0), AddressField::kHeight);
}

// Return loads what the address holds (the Swift's onSubmit); Esc gives the
// address back to the page (onExitCommand). Both end the editing.
bool ToolbarView::HandleKeyEvent(views::Textfield* sender,
                                 const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed ||
      key_event.IsCommandDown() || key_event.IsControlDown() ||
      key_event.IsAltDown()) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    callbacks_.address_entered.Run(address());
    EndEditing();
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    address_->ShowAddress(address_url_);
    callbacks_.address_left.Run();
    EndEditing();
    return true;
  }
  return false;
}

void ToolbarView::EndEditing() {
  views::FocusManager* focus =
      GetWidget() ? GetWidget()->GetFocusManager() : nullptr;
  if (focus && address_->HasFocus()) {
    focus->ClearFocus();
  }
}

BEGIN_METADATA(ToolbarView)
END_METADATA

}  // namespace pane
