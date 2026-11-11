// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/page/find_bar_view.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/field.h"
#include "pane/ui/design/icon_button.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/view_ids.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/border.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/textfield/textfield.h"

namespace pane {

namespace {

// Each button is a 32-point circle: the chevrons 4 apart, close right after
// them; the HStack's 8 between the field, the count, and the chevrons.
constexpr int kButton = 32;
constexpr int kChevronGap = 4;
constexpr int kSpacing = 8;
// The card's content, 16 in from its leading edge and 6 from its trailing.
constexpr int kLeading = 16;
constexpr int kTrailing = 6;

// The field's text and the count's, at 13 and 12 (FindBar.swift).
constexpr TextStyle kFieldText{13, weight::kRegular, 0};
constexpr TextStyle kCountText{12, weight::kRegular, 0};

// The query's field: its text grayscale rather than subpixel, as Pane's
// labels (ADR 0060).
class FindField : public views::Textfield {
  METADATA_HEADER(FindField, views::Textfield)

 public:
  FindField() { GetRenderText()->set_subpixel_rendering_suppressed(true); }
};

BEGIN_METADATA(FindField)
END_METADATA

}  // namespace

FindBarView::Callbacks::Callbacks() = default;
FindBarView::Callbacks::Callbacks(Callbacks&&) = default;
FindBarView::Callbacks& FindBarView::Callbacks::operator=(Callbacks&&) =
    default;
FindBarView::Callbacks::~Callbacks() = default;

DialogCard::Look FindBarView::CardLook() {
  DialogCard::Look look;
  // The scale's 15: Aside's circle of 8 (ADR 0057).
  look.radius = radius::kLg;
  look.fill = kPaneColorPopover;
  // A hairline inside the edge, which only dark draws, as SwiftUI's
  // strokeBorder.
  look.edge = kPaneColorFindBarRing;
  look.edge_width = 1;
  look.edge_outside = false;
  look.padding = gfx::Insets::TLBR(0, kLeading, 0, kTrailing);
  // SwiftUI's shadow(radius: 6, y: 6).
  look.shadow = kPaneColorFindBarShadow;
  look.shadow_offset = gfx::Vector2dF(0, 6);
  look.shadow_sigma = 6;
  return look;
}

FindBarView::FindBarView(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
  SetID(kViewIdFind);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);

  // The query: the field draws on the bar's own fill, with none of
  // Chromium's background, border, ring, or ink, its caret and text the
  // foreground's.
  auto field = std::make_unique<FindField>();
  field->SetID(kViewIdFindField);
  field->set_controller(this);
  field->SetBackgroundEnabled(false);
  field->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  views::FocusRing::Remove(field.get());
  views::InkDrop::Get(field.get())
      ->SetMode(views::InkDropHost::InkDropMode::OFF);
  field->SetFontList(SystemFont(kFieldText.size, kFieldText.weight));
  field->SetTextColorId(kPaneColorForeground);
  // Aside's field shows no placeholder; VoiceOver reads its name.
  field->SetAccessibleName(u"Find in page");
  field_ = AddChildView(std::move(field));
  selection_ = std::make_unique<SelectionFollowsWindow>(field_);

  count_ = AddChildView(
      std::make_unique<DigitCellsLabel>(kCountText, kPaneColorMutedForeground));
  count_->SetID(kViewIdFindCount);
  count_->SetVisible(false);

  previous_ = AddChildView(std::make_unique<IconButton>(
      base::BindRepeating(
          [](FindBarView* bar) { bar->callbacks_.step.Run(false); },
          base::Unretained(this)),
      kButton, IconButton::Glyph{Icon::kChevronUp, 18, 2}, u"Previous match"));
  previous_->SetID(kViewIdFindPrevious);
  next_ = AddChildView(std::make_unique<IconButton>(
      base::BindRepeating(
          [](FindBarView* bar) { bar->callbacks_.step.Run(true); },
          base::Unretained(this)),
      kButton, IconButton::Glyph{Icon::kChevronDown, 18, 2}, u"Next match"));
  next_->SetID(kViewIdFindNext);
  close_ = AddChildView(std::make_unique<IconButton>(
      callbacks_.close, kButton, IconButton::Glyph{Icon::kX, 10, 2.5f},
      u"Close find bar"));
  close_->SetID(kViewIdFindClose);
  SetState(State());
}

FindBarView::~FindBarView() {
  selection_.reset();
}

void FindBarView::SetState(const State& state) {
  state_ = state;
  if (field_->GetText() != state.text) {
    field_->SetText(state.text);
    searched_ = state.text;
    if (field_->HasFocus()) {
      field_->SelectAll(false);
    }
  }
  // The count once something was found for the text, the text's own: until
  // then the last one stays, as it did while the next search ran.
  const bool counts = !state.text.empty();
  if (counts && state.matches) {
    count_->SetText(base::StrCat({base::NumberToString16(state.active), u"/",
                                  base::NumberToString16(*state.matches)}));
    count_->SetColor(*state.matches == 0 ? kPaneColorDestructive
                                         : kPaneColorMutedForeground);
  }
  const bool shows_count = counts && !count_->text().empty();
  if (!counts) {
    count_->SetText(std::u16string());
  }
  if (count_->GetVisible() != shows_count) {
    count_->SetVisible(shows_count);
  }
  const bool can_step = counts && state.matches.value_or(0) > 0;
  previous_->SetEnabled(can_step);
  next_->SetEnabled(can_step);
  InvalidateLayout();
}

void FindBarView::FocusField() {
  if (field_->HasFocus()) {
    return;
  }
  field_->RequestFocus();
  field_->SelectAll(false);
}

void FindBarView::Type(const std::u16string& text) {
  field_->SetText(text);
  OnAfterUserAction(field_);
}

std::vector<std::pair<std::string, const views::View*>>
FindBarView::NamedViews() const {
  std::vector<std::pair<std::string, const views::View*>> named;
  for (const views::View* view :
       {static_cast<const views::View*>(field_.get()),
        static_cast<const views::View*>(count_.get()),
        static_cast<const views::View*>(previous_.get()),
        static_cast<const views::View*>(next_.get()),
        static_cast<const views::View*>(close_.get())}) {
    named.emplace_back(std::string(ViewIdName(view->GetID())), view);
  }
  return named;
}

gfx::Size FindBarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(kWidth - kLeading - kTrailing, kHeight);
}

// From the trailing edge: close, then the chevrons 4 apart, then the count
// 8 before them, and the field from the leading edge to 8 before what
// follows it, each centered top to bottom (the Swift's HStack).
void FindBarView::Layout(PassKey) {
  const auto centered = [&](views::View* view, int x, int width) {
    const int view_height =
        std::min(view->GetPreferredSize().height(), height());
    view->SetBounds(x, (height() - view_height) / 2, width, view_height);
  };
  int x = width() - kButton;
  close_->SetBounds(x, (height() - kButton) / 2, kButton, kButton);
  x -= kButton;
  next_->SetBounds(x, (height() - kButton) / 2, kButton, kButton);
  x -= kChevronGap + kButton;
  previous_->SetBounds(x, (height() - kButton) / 2, kButton, kButton);
  x -= kSpacing;
  if (count_->GetVisible()) {
    const int count_width = count_->GetPreferredSize().width();
    centered(count_, x - count_width, count_width);
    x -= count_width + kSpacing;
  }
  centered(field_, 0, std::max(x, 0));
}

bool FindBarView::HandleKeyEvent(views::Textfield* sender,
                                 const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed ||
      key_event.IsCommandDown() || key_event.IsControlDown() ||
      key_event.IsAltDown()) {
    return false;
  }
  switch (key_event.key_code()) {
    case ui::VKEY_RETURN:
      callbacks_.step.Run(!key_event.IsShiftDown());
      return true;
    case ui::VKEY_ESCAPE:
      callbacks_.close.Run();
      return true;
    default:
      return false;
  }
}

// As Chrome's FindBarView: a search once the text changed, and not while an
// input method composes it, whose text isn't what is looked for yet.
void FindBarView::OnAfterUserAction(views::Textfield* sender) {
  if (sender->IsIMEComposing() || sender->GetText() == searched_) {
    return;
  }
  searched_ = sender->GetText();
  callbacks_.find.Run(searched_);
}

// A paste searches what was pasted, even the same text again.
void FindBarView::OnAfterPaste() {
  searched_.clear();
}

BEGIN_METADATA(FindBarView)
END_METADATA

}  // namespace pane
