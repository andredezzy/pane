// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/dialog.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/draw_looper.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// DialogHeading's title and message, 6 apart.
constexpr TextStyle kTitle{18, weight::kSemibold, 24};
constexpr TextStyle kMessage{14, weight::kRegular, 20};
constexpr int kHeadingSpacing = 6;

// The card: its content 24 in, rounded 29, its ring a point outside it,
// rounded a point more; its shadow 0 24px 64px, a Gaussian of 32
// (surfaceBackground's blur over 2, as the content panel's).
constexpr int kPadding = 24;
constexpr int kRing = 1;
constexpr gfx::Vector2dF kShadowOffset(0, 24);
constexpr float kShadowSigma = 32;
// How small the card starts, and goes.
constexpr float kScale = 0.97f;

float ScaleOf(const views::Widget* widget) {
  return widget && widget->GetCompositor()
             ? widget->GetCompositor()->device_scale_factor()
             : 1;
}

}  // namespace

DialogHeading::DialogHeading(const std::u16string& title,
                             const std::u16string& message,
                             ui::ColorVariant message_color) {
  title_ = AddChildView(MakeLabel(title, kTitle, kPaneColorForeground));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  message_ =
      AddChildView(MakeLabel(message, kMessage, std::move(message_color)));
  message_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  message_->SetMultiLine(true);
}

DialogHeading::~DialogHeading() = default;

int DialogHeading::HeightForWidth(int width) const {
  return kTitle.line_height + kHeadingSpacing +
         message_->GetHeightForWidth(width);
}

gfx::Size DialogHeading::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int width = available_size.width().is_bounded()
                        ? available_size.width().value()
                        : std::max(title_->GetPreferredSize().width(),
                                   message_->GetPreferredSize().width());
  return gfx::Size(width, HeightForWidth(width));
}

void DialogHeading::Layout(PassKey) {
  title_->SetBounds(0, 0, width(), kTitle.line_height);
  const int top = kTitle.line_height + kHeadingSpacing;
  message_->SetBounds(0, top, width(), message_->GetHeightForWidth(width()));
}

BEGIN_METADATA(DialogHeading)
END_METADATA

// The card on its own layer, which grows as the dialog comes and shrinks as it
// goes: its fill and its ring, outside it. A press on it stays with it.
class ModalDialog::Card : public views::View {
  METADATA_HEADER(Card, views::View)

 public:
  Card() {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    gfx::RectF ring(GetLocalBounds());
    gfx::RectF card = ring;
    card.Inset(kRing);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kPaneColorCard));
    canvas->DrawPath(SquirclePath(card, radius::k2xl), flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(kRing);
    flags.setColor(colors->GetColor(kPaneColorBorder));
    canvas->DrawPath(SquircleStrokePath(ring, radius::k2xl + kRing, kRing),
                     flags);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override { return true; }
};

BEGIN_METADATA(ModalDialog, Card)
END_METADATA

ModalDialog::Callbacks::Callbacks() = default;
ModalDialog::Callbacks::Callbacks(Callbacks&&) = default;
ModalDialog::Callbacks& ModalDialog::Callbacks::operator=(Callbacks&&) =
    default;
ModalDialog::Callbacks::~Callbacks() = default;

ModalDialog::ModalDialog(int width,
                         std::unique_ptr<views::View> content,
                         Callbacks callbacks)
    : width_(width),
      callbacks_(std::move(callbacks)),
      motion_(0,
              base::BindRepeating(&ModalDialog::Step, base::Unretained(this))) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  // Unseen until it comes in, and made ahead of it.
  layer()->SetOpacity(0);
  SetVisible(false);
  // It holds the keys, with no ring of its own; its content's controls take
  // them by Tab.
  SetFocusBehavior(FocusBehavior::ALWAYS);
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetIsModal(true);
  card_ = AddChildView(std::make_unique<Card>());
  content_ = card_->AddChildView(std::move(content));
}

ModalDialog::~ModalDialog() = default;

gfx::RectF ModalDialog::CardPlace() const {
  const int inner = width_ - 2 * kPadding;
  const float height = content_->GetHeightForWidth(inner) + 2 * kPadding;
  // In the middle, as SwiftUI centers it: to the half point at 2×.
  gfx::RectF card((this->width() - width_) / 2.0f,
                  (this->height() - height) / 2.0f, width_, height);
  card.Outset(kRing);
  return card;
}

gfx::Rect ModalDialog::CardBounds() const {
  return card_->bounds();
}

void ModalDialog::Layout(PassKey) {
  const gfx::RectF place = CardPlace();
  card_->SetBounds(std::floor(place.x()), std::floor(place.y()),
                   std::round(place.width()), std::round(place.height()));
  content_->SetBounds(kRing + kPadding, kRing + kPadding, width_ - 2 * kPadding,
                      card_->height() - 2 * (kRing + kPadding));
  // What the whole points leave of the card's place, on the screen's pixels.
  const float scale = ScaleOf(GetWidget());
  const auto rest = [&](float at, float whole) {
    return std::round((at - whole) * scale) / scale;
  };
  shift_ =
      gfx::Vector2dF(rest(place.x(), card_->x()), rest(place.y(), card_->y()));
  Step(motion_.value());
}

void ModalDialog::Step(double shown) {
  layer()->SetOpacity(shown);
  // The card's place, and its growth about its middle.
  gfx::Transform transform =
      gfx::Transform::MakeTranslation(shift_.x(), shift_.y());
  const float grown = kScale + (1 - kScale) * shown;
  if (grown != 1) {
    const gfx::PointF middle(card_->width() / 2.0f, card_->height() / 2.0f);
    transform.Translate(middle.x(), middle.y());
    transform.Scale(grown, grown);
    transform.Translate(-middle.x(), -middle.y());
  }
  card_->layer()->SetTransform(transform);
  if (closing_ && shown == 0 && !motion_.is_moving()) {
    SetVisible(false);
  }
}

bool ModalDialog::showing() const {
  return GetVisible() && !closing_;
}

void ModalDialog::AnimateIn() {
  closing_ = false;
  SetVisible(true);
  SetCanProcessEventsWithinSubtree(true);
  ResetAccelerators();
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
  AddAccelerator(ui::Accelerator(ui::VKEY_RETURN, ui::EF_NONE));
  RequestFocus();
  motion_.MoveTo(1, motion::Panel(),
                 GetWidget() ? GetWidget()->GetCompositor() : nullptr);
}

void ModalDialog::AnimateOut() {
  if (!showing()) {
    return;
  }
  closing_ = true;
  SetCanProcessEventsWithinSubtree(false);
  ResetAccelerators();
  motion_.MoveTo(0, motion::Panel(),
                 GetWidget() ? GetWidget()->GetCompositor() : nullptr);
}

void ModalDialog::OnPaint(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  canvas->DrawColor(colors->GetColor(kPaneColorModalScrim));
  // The card's shadow, under where the card's layer draws it.
  gfx::RectF card(card_->bounds());
  card.Inset(kRing);
  cc::DrawLooperBuilder shadow;
  shadow.AddShadow(
      {kShadowOffset.x(), kShadowOffset.y()}, kShadowSigma,
      SkColor4f::FromColor(colors->GetColor(kPaneColorDialogShadow)),
      cc::DrawLooper::kOverrideAlphaFlag);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(colors->GetColor(kPaneColorCard));
  flags.setLooper(shadow.Detach());
  canvas->DrawPath(SquirclePath(card, radius::k2xl), flags);
}

bool ModalDialog::OnMousePressed(const ui::MouseEvent& event) {
  // A press on the scrim, the card's taking its own.
  if (callbacks_.dismiss) {
    callbacks_.dismiss.Run();
  }
  return true;
}

bool ModalDialog::AcceleratorPressed(const ui::Accelerator& accelerator) {
  const base::RepeatingClosure& run = accelerator.key_code() == ui::VKEY_ESCAPE
                                          ? callbacks_.dismiss
                                          : callbacks_.accept;
  if (run) {
    run.Run();
  }
  return true;
}

BEGIN_METADATA(ModalDialog)
END_METADATA

}  // namespace pane
