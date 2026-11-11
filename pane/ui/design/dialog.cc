// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/dialog.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/draw_looper.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/placement.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_solid_color.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/insets_f.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_targeter.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// DialogHeading's title and message, 6 apart.
constexpr TextStyle kTitle{18, weight::kSemibold, 24};
constexpr TextStyle kMessage{14, weight::kRegular, 20};
constexpr int kHeadingSpacing = 6;

// ModalDialog's card: its content 24 in, its shadow 0 24px 64px.
constexpr int kPadding = 24;
constexpr gfx::Vector2dF kShadowOffset(0, 24);
constexpr float kShadowSigma = 32;
// How small the card starts, and goes.
constexpr float kScale = 0.97f;

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

DialogCard::Look::Look() = default;
DialogCard::Look::Look(const Look&) = default;
DialogCard::Look& DialogCard::Look::operator=(const Look&) = default;
DialogCard::Look::~Look() = default;

// The card in its shadow's layer: its fill and its edge. A press on it stays
// with it.
class DialogCard::Fill : public views::View {
  METADATA_HEADER(Fill, views::View)

 public:
  explicit Fill(const Look& look) : look_(look) {}

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const gfx::RectF bounds(GetLocalBounds());
    gfx::RectF fill = bounds;
    if (look_.edge_outside) {
      fill.Inset(look_.edge_width);
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    // In pieces, so a tile inside the card rasters no path (ADR 0123).
    flags.setColor(look_.fill.ResolveToSkColor(colors));
    FillSquircle(*canvas, fill, look_.radius, flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(look_.edge_width);
    flags.setColor(look_.edge.ResolveToSkColor(colors));
    if (look_.edge_outside) {
      StrokeSquircleBorder(*canvas, bounds, look_.radius + look_.edge_width,
                           look_.edge_width, flags);
    } else {
      StrokeSquircleBorder(*canvas, fill, look_.radius, look_.edge_width,
                           flags);
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override { return true; }

 private:
  const Look look_;
};

BEGIN_METADATA(DialogCard, Fill)
END_METADATA

DialogCard::DialogCard(const Look& look, std::unique_ptr<views::View> content)
    : look_(look) {
  // The shadow's layer, sized to how far the shadow reaches around the card,
  // so only that much is drawn as the dialog shows. The card paints over it
  // in the same layer, one layer's tiles where two rastered its middle twice
  // (ADR 0123).
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetOpacity(0);
  SetEventTargeter(std::make_unique<views::ViewTargeter>(this));
  fill_ = AddChildView(std::make_unique<Fill>(look_));
  content_ = fill_->AddChildView(std::move(content));
}

DialogCard::~DialogCard() = default;

gfx::Insets DialogCard::Reach() const {
  const int blur = std::ceil(3 * look_.shadow_sigma);
  const int down = std::ceil(look_.shadow_offset.y());
  return gfx::Insets::TLBR(std::max(blur - down, 0), blur, blur + down, blur);
}

gfx::SizeF DialogCard::SizeFor(int width) const {
  const float outside = look_.edge_outside ? look_.edge_width : 0;
  const int inner = width - look_.padding.width();
  return gfx::SizeF(width + 2 * outside, content_->GetHeightForWidth(inner) +
                                             look_.padding.height() +
                                             2 * outside);
}

bool DialogCard::Place(const gfx::RectF& place) {
  const gfx::Rect before = bounds();
  const gfx::Vector2dF shift_before = shift_;
  const gfx::Rect card(std::floor(place.x()), std::floor(place.y()),
                       std::round(place.width()), std::round(place.height()));
  const gfx::Insets reach = Reach();
  gfx::Rect bounds = card;
  bounds.Inset(-reach);
  SetBoundsRect(bounds);
  fill_->SetBounds(reach.left(), reach.top(), card.width(), card.height());
  const int outside = look_.edge_outside ? std::ceil(look_.edge_width) : 0;
  content_->SetBounds(
      outside + look_.padding.left(), outside + look_.padding.top(),
      std::max(card.width() - 2 * outside - look_.padding.width(), 0),
      std::max(card.height() - 2 * outside - look_.padding.height(), 0));
  // What the whole points leave of the card's place, on the screen's pixels.
  const float scale = ScaleOf(GetWidget());
  const auto rest = [&](float at, float whole) {
    return std::round((at - whole) * scale) / scale;
  };
  shift_ = gfx::Vector2dF(rest(place.x(), card.x()), rest(place.y(), card.y()));
  return this->bounds() != before || shift_ != shift_before;
}

gfx::Rect DialogCard::CardBounds() const {
  return fill_->bounds() + bounds().OffsetFromOrigin();
}

gfx::Transform DialogCard::TransformOf(bool shown) const {
  // The card's place, and its growth about its middle, its shadow's with it.
  gfx::Transform transform =
      gfx::Transform::MakeTranslation(shift_.x(), shift_.y());
  if (!shown) {
    const gfx::PointF middle = gfx::RectF(fill_->bounds()).CenterPoint();
    transform.Translate(middle.x(), middle.y());
    transform.Scale(kScale, kScale);
    transform.Translate(-middle.x(), -middle.y());
  }
  return transform;
}

void DialogCard::Settle(bool shown) {
  layer()->SetOpacity(shown ? 1 : 0);
  layer()->SetTransform(TransformOf(shown));
}

void DialogCard::Move(bool shown, base::OnceClosure done) {
  // The card and its shadow grow, or shrink, as they fade: on the
  // compositor's animation, from where they stand.
  CompositorMotion::Start(
      layer(), motion::Panel(),
      {.opacity = shown ? 1.0f : 0.0f, .transform = TransformOf(shown)},
      std::move(done));
}

void DialogCard::OnPaint(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  gfx::RectF card(GetLocalBounds());
  card.Inset(gfx::InsetsF(Reach()));
  if (look_.edge_outside) {
    card.Inset(look_.edge_width);
  }
  // What a translucent fill covers casts its shadow as much lighter.
  const SkColor fill = look_.fill.ResolveToSkColor(colors);
  const SkColor shadow_color = look_.shadow.ResolveToSkColor(colors);
  const SkColor shadow = SkColorSetA(
      shadow_color, SkColorGetA(shadow_color) * SkColorGetA(fill) / 255);
  cc::DrawLooperBuilder looper;
  looper.AddShadow({look_.shadow_offset.x(), look_.shadow_offset.y()},
                   look_.shadow_sigma, SkColor4f::FromColor(shadow),
                   cc::DrawLooper::kOverrideAlphaFlag);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(fill);
  flags.setLooper(looper.Detach());
  // Cast by the card's shape as a layer rounds it, a circle of its radius's
  // circular equivalent (ADR 0057): blurred this wide, that is the
  // squircle's shadow within a 255th, and Skia blurs a rounded rect once, a
  // corner's worth, where it works a path's blur out again for each tile the
  // window rasters (ADR 0123).
  canvas->DrawRoundRect(card, look_.radius * kCircularEquivalent, flags);
}

bool DialogCard::DoesIntersectRect(const views::View* target,
                                   const gfx::Rect& rect) const {
  return fill_->bounds().Intersects(rect);
}

BEGIN_METADATA(DialogCard)
END_METADATA

ModalDialog::Callbacks::Callbacks() = default;
ModalDialog::Callbacks::Callbacks(Callbacks&&) = default;
ModalDialog::Callbacks& ModalDialog::Callbacks::operator=(Callbacks&&) =
    default;
ModalDialog::Callbacks::~Callbacks() = default;

ModalDialog::ModalDialog(int width,
                         std::unique_ptr<views::View> content,
                         Callbacks callbacks,
                         Keys keys)
    : width_(width), callbacks_(std::move(callbacks)), keys_(keys) {
  // A layer that draws nothing, over what it covers. The scrim and the card
  // fade apart, each on a layer of its own, as the Swift's modal() faded its
  // scrim and its content: faded together, they took the compositor a pass
  // the window's size at each frame of the entrance (ADR 0123).
  SetPaintToLayer(ui::LAYER_NOT_DRAWN);
  // Unseen until it comes in, and made ahead of it.
  SetVisible(false);
  // It holds the keys, with no ring of its own; its content's controls take
  // them by Tab.
  SetFocusBehavior(FocusBehavior::ALWAYS);
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetIsModal(true);
  // The scrim: a color the compositor draws, with nothing to paint. A press
  // on it is the dialog's.
  scrim_ = AddChildView(std::make_unique<views::View>());
  scrim_->SetPaintToLayer(ui::LAYER_SOLID_COLOR);
  scrim_->layer()->SetFillsBoundsOpaquely(false);
  scrim_->layer()->SetOpacity(0);
  scrim_->SetCanProcessEventsWithinSubtree(false);
  // The card: its content 24 in, rounded 29, on a 1-point ring outside it
  // and its shadow, 0 24px 64px, a Gaussian of 32 (surfaceBackground's blur
  // over 2, as the content panel's).
  DialogCard::Look look;
  look.fill = kPaneColorCard;
  look.edge = kPaneColorBorder;
  look.padding = gfx::Insets(kPadding);
  look.shadow = kPaneColorDialogShadow;
  look.shadow_offset = kShadowOffset;
  look.shadow_sigma = kShadowSigma;
  card_ = AddChildView(std::make_unique<DialogCard>(look, std::move(content)));
}

ModalDialog::~ModalDialog() = default;

bool ModalDialog::Place() {
  scrim_->SetBoundsRect(GetLocalBounds());
  // In the middle, as SwiftUI centers it: to the half point at 2×.
  const gfx::SizeF size = card_->SizeFor(width_);
  return card_->Place(gfx::RectF((width() - size.width()) / 2,
                                 (height() - size.height()) / 2, size.width(),
                                 size.height()));
}

void ModalDialog::Layout(PassKey) {
  const bool moved = Place();
  if (!moving_) {
    Settle();
  } else if (moved) {
    // The window changed under the entrance: on to the new place.
    Move(shown_);
  }
}

void ModalDialog::Settle() {
  scrim_->layer()->SetOpacity(shown_ ? 1 : 0);
  card_->Settle(shown_);
}

void ModalDialog::Move(bool shown) {
  shown_ = shown;
  moving_ = true;
  // The card grows, or shrinks, as it fades, and the scrim fades with it.
  card_->Move(shown);
  CompositorMotion::Start(
      scrim_->layer(), motion::Panel(), {.opacity = shown ? 1.0f : 0.0f},
      base::BindOnce(&ModalDialog::Moved, weak_factory_.GetWeakPtr()));
}

void ModalDialog::Moved() {
  moving_ = false;
  if (closing_ && !shown_) {
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
  // Laid out now, so the card grows from its own middle; from gone, unless
  // it is on its way out.
  Place();
  if (!moving_) {
    Settle();
  }
  Move(true);
}

void ModalDialog::AnimateOut() {
  if (!showing()) {
    return;
  }
  closing_ = true;
  SetCanProcessEventsWithinSubtree(false);
  ResetAccelerators();
  Move(false);
}

void ModalDialog::OnThemeChanged() {
  views::View::OnThemeChanged();
  // Black in both appearances, darker on dark.
  scrim_->layer()->AsSolidColor()->SetColor(
      SkColor4f::FromColor(GetColorProvider()->GetColor(kPaneColorModalScrim)));
}

bool ModalDialog::OnMousePressed(const ui::MouseEvent& event) {
  // A press on the scrim, or on the card's shadow; the card takes its own.
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

// Taking every key, it holds the focus: the FocusManager neither moves it by
// Tab nor runs an accelerator with it, and the keys come here.
bool ModalDialog::SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) {
  return takes_every_key();
}

bool ModalDialog::OnKeyPressed(const ui::KeyEvent& event) {
  if (!takes_every_key()) {
    return false;
  }
  // Esc, whatever modifier is down, as the Swift's KeyRouting matched it;
  // Return, or the keypad's Enter. Any other key stops here.
  if (event.key_code() == ui::VKEY_ESCAPE) {
    AcceleratorPressed(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
  } else if (event.key_code() == ui::VKEY_RETURN) {
    AcceleratorPressed(ui::Accelerator(ui::VKEY_RETURN, ui::EF_NONE));
  }
  return true;
}

bool ModalDialog::OnKeyReleased(const ui::KeyEvent& event) {
  return takes_every_key();
}

BEGIN_METADATA(ModalDialog)
END_METADATA

}  // namespace pane
