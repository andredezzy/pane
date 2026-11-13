// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/toolbar/address_field.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "pane/model/address_input.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/toolbar/toolbar_progress_bar.h"
#include "pane/ui/view_ids.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/compositor.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/render_text.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/border.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// What Return and the field's text would load show as nothing else: the
// placeholder, muted.
constexpr char16_t kPrompt[] = u"Search or enter URL";

// The text 11 in from each edge, 10 inside the 1-point ring the edited field
// shows (Paper's 04.8, CSS's px-2.5 within it).
constexpr int kTextInset = 11;

// At rest, Aside's: the domain 14 in, the divider and the title each 12
// after what comes before, 10 clear of the end. The divider is half a point
// wide and 18 tall.
constexpr float kLabelLeading = 14;
constexpr float kLabelSpacing = 12;
constexpr int kLabelTrailing = 10;
constexpr float kDividerWidth = 0.5f;
constexpr float kDividerHeight = 18;

// A text's width on a screen of `scale` pixels to the point, as SwiftUI lays
// it out: rounded up to the pixel.
float OnPixels(float width, float scale) {
  return std::ceil(width * scale) / scale;
}

}  // namespace

// The field at rest: the domain in the text color, a divider, and the title
// muted, all at 12 (the Swift's AddressLabel). Presses go through it to the
// field under it.
class AddressField::Label : public views::View {
  METADATA_HEADER(Label, views::View)

 public:
  Label() {
    SetCanProcessEventsWithinSubtree(false);
    domain_ = AddChildView(
        MakeLabel(std::u16string(), text::kSidebar, kPaneColorForeground));
    domain_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_ = AddChildView(
        MakeLabel(std::u16string(), text::kSidebar, kPaneColorMutedForeground));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetElideBehavior(gfx::ELIDE_TAIL);
  }

  void Set(const model::AddressLabel& label) {
    domain_->SetText(base::UTF8ToUTF16(label.domain));
    title_->SetText(base::UTF8ToUTF16(label.title));
    title_->SetVisible(!label.title.empty());
    InvalidateLayout();
    SchedulePaint();
  }

  // views::View:
  void Layout(PassKey) override {
    const int line = text::kSidebar.line_height;
    const int top = (height() - line) / 2;
    // The domain at its own width, as SwiftUI's fixedSize, never cut.
    const int domain_width = domain_->GetPreferredSize().width();
    domain_->SetBounds(kLabelLeading, top, domain_width, line);
    divider_x_ =
        kLabelLeading +
        OnPixels(gfx::GetStringWidthF(domain_->GetText(), domain_->font_list()),
                 Scale()) +
        kLabelSpacing;
    const int title_x = static_cast<int>(
        std::round(divider_x_ + kDividerWidth + kLabelSpacing));
    title_->SetBounds(title_x, top,
                      std::max(width() - kLabelTrailing - title_x, 0), line);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors || !title_->GetVisible()) {
      return;
    }
    canvas->DrawRect(gfx::RectF(divider_x_, (height() - kDividerHeight) / 2,
                                kDividerWidth, kDividerHeight),
                     colors->GetColor(kPaneColorAddressDivider));
  }

 private:
  float Scale() const {
    const views::Widget* widget = GetWidget();
    return widget && widget->GetCompositor()
               ? widget->GetCompositor()->device_scale_factor()
               : 2;
  }

  raw_ptr<views::Label> domain_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  float divider_x_ = 0;
};

BEGIN_METADATA(AddressField, Label)
END_METADATA

AddressField::AddressField(model::ToolbarProgress* progress)
    : hover_(0,
             base::BindRepeating(
                 [](AddressField* field, double) { field->SchedulePaint(); },
                 base::Unretained(this))) {
  SetID(kViewIdAddress);
  // On the field alone: the domain and title over it keep their own.
  SetAccessibleName(u"Address");
  // Its fill, ring, and bar are this one's, so Chromium's own background,
  // border, ring, and hover go.
  SetBackgroundEnabled(false);
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, kTextInset)));
  views::FocusRing::Remove(this);
  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
  SetFontList(SystemFont(text::kSidebar.size, text::kSidebar.weight));
  SetTextColorId(kPaneColorForeground);
  SetPlaceholderText(kPrompt);
  SetPlaceholderTextColorId(kPaneColorMutedForeground);
  // Grayscale rather than subpixel text, as StyleLabel's (ADR 0060).
  GetRenderText()->set_subpixel_rendering_suppressed(true);
  set_placeholder_text_draw_flags(placeholder_text_draw_flags() |
                                  gfx::Canvas::NO_SUBPIXEL_RENDERING);

  label_ = AddChildView(std::make_unique<Label>());
  label_->SetVisible(false);
  bar_ = AddChildView(std::make_unique<ToolbarProgressBar>(progress));
}

AddressField::~AddressField() = default;

void AddressField::ShowAddress(const std::u16string& address) {
  shown_ = address;
  SetText(address);
  if (HasFocus()) {
    SelectAll(false);
  }
  UpdateLabel(HasFocus());
}

bool AddressField::IsTypedOver() const {
  return GetText() != shown_;
}

void AddressField::SetTitle(const std::u16string& title) {
  if (title == title_) {
    return;
  }
  title_ = title;
  UpdateLabel(HasFocus());
}

void AddressField::UpdateLabel(bool editing) {
  const std::optional<model::AddressLabel> label =
      editing ? std::nullopt
              : model::AddressLabelFor(base::UTF16ToUTF8(GetText()),
                                       base::UTF16ToUTF8(title_));
  const bool shown = label.has_value();
  if (label) {
    label_->Set(*label);
  }
  if (shown != label_->GetVisible()) {
    label_->SetVisible(shown);
    // The field's own text hides under the label, and shows again.
    SchedulePaint();
  }
}

// A press that gives the field the focus places its caret, then selects all
// of it, as the Swift's did a turn after: a key that comes next replaces the
// address. A press into the field already being edited places the caret.
bool AddressField::OnMousePressed(const ui::MouseEvent& event) {
  const bool focusing = !HasFocus() && event.IsOnlyLeftMouseButton() &&
                        !event.IsShiftDown() && event.GetClickCount() == 1;
  if (focusing && GetWidget()) {
    LogInteraction(Interaction::kClickAddressField,
                   GetWidget()->GetCompositor());
  }
  pressing_ = focusing;
  const bool handled = views::Textfield::OnMousePressed(event);
  pressing_ = false;
  selected_by_press_ = focusing && HasFocus();
  if (selected_by_press_) {
    press_point_ = event.location();
    SelectAll(false);
  }
  return handled;
}

// A drag from that press selects from where it began, once it goes farther
// than a click's jitter; until then all of it stays selected.
bool AddressField::OnMouseDragged(const ui::MouseEvent& event) {
  if (selected_by_press_) {
    if (!ExceededDragThreshold(event.location() - press_point_)) {
      return true;
    }
    selected_by_press_ = false;
    SetSelectedRange(gfx::Range(
        GetRenderText()->FindCursorPosition(press_point_).caret_pos()));
  }
  return views::Textfield::OnMouseDragged(event);
}

void AddressField::OnMouseEntered(const ui::MouseEvent& event) {
  views::Textfield::OnMouseEntered(event);
  hover_.MoveTo(1, motion::Hover(),
                GetWidget() ? GetWidget()->GetCompositor() : nullptr);
}

void AddressField::OnMouseExited(const ui::MouseEvent& event) {
  views::Textfield::OnMouseExited(event);
  hover_.MoveTo(0, motion::Hover(),
                GetWidget() ? GetWidget()->GetCompositor() : nullptr);
}

// The keys came by Tab, ⌘L, or the window coming back: all of it selected.
// A press selects once its caret is placed (OnMousePressed).
void AddressField::OnFocus() {
  views::Textfield::OnFocus();
  if (!pressing_) {
    SelectAll(false);
  }
  UpdateLabel(/*editing=*/true);
  SchedulePaint();
}

void AddressField::OnBlur() {
  views::Textfield::OnBlur();
  selected_by_press_ = false;
  UpdateLabel(/*editing=*/false);
  SchedulePaint();
}

bool AddressField::label_shown() const {
  return label_->GetVisible();
}

// At rest under the label, the field draws its fill alone; its text, the
// selection, and the placeholder only while they show.
void AddressField::OnPaint(gfx::Canvas* canvas) {
  if (label_shown()) {
    OnPaintBackground(canvas);
    OnPaintBorder(canvas);
    return;
  }
  views::Textfield::OnPaint(canvas);
}

void AddressField::OnPaintBackground(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors || hover_.value() <= 0) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(colors->GetColor(kPaneColorAddressHover));
  // The fill fades in as a whole, as SwiftUI's animated opacity.
  flags.setAlphaf(flags.getAlphaf() * static_cast<float>(hover_.value()));
  canvas->DrawPath(SquirclePath(gfx::RectF(GetLocalBounds()), radius::kTab),
                   flags);
}

// Being edited: the ring's 1-point line just inside the edge (SwiftUI's
// strokeBorder).
void AddressField::OnPaintBorder(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors || !HasFocus()) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1);
  flags.setColor(colors->GetColor(kPaneColorRing));
  canvas->DrawPath(
      SquircleStrokePath(gfx::RectF(GetLocalBounds()), radius::kTab, 1), flags);
}

void AddressField::Layout(PassKey) {
  label_->SetBoundsRect(GetLocalBounds());
  bar_->SetBounds(0, height() - ToolbarProgressBar::kHeight, width(),
                  ToolbarProgressBar::kHeight);
}

BEGIN_METADATA(AddressField)
END_METADATA

}  // namespace pane
