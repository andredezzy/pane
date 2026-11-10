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

// The card, painted in its shadow's layer, which grows as the dialog comes and
// shrinks as it goes: its fill and its ring, outside it. A press on it stays
// with it.
class ModalDialog::Card : public views::View {
  METADATA_HEADER(Card, views::View)

 public:
  Card() = default;

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
    // In pieces, so a tile inside the card rasters no path (ADR 0123).
    flags.setColor(colors->GetColor(kPaneColorCard));
    FillSquircle(*canvas, card, radius::k2xl, flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(kRing);
    flags.setColor(colors->GetColor(kPaneColorBorder));
    StrokeSquircleBorder(*canvas, ring, radius::k2xl + kRing, kRing, flags);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override { return true; }
};

BEGIN_METADATA(ModalDialog, Card)
END_METADATA

// The card's shadow on a layer of its own, sized to how far the shadow
// reaches around the card, so only that much is drawn as the dialog shows:
// the scrim is a color the compositor draws, where it was a whole window
// rastered at each showing. The card paints over it in the same layer, one
// layer's tiles where two rastered its middle twice (ADR 0123). A press on
// the shadow is the scrim's.
class ModalDialog::Shadow : public views::View {
  METADATA_HEADER(Shadow, views::View)

 public:
  Shadow() {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }

  // How far past the card, ring included, the shadow reaches: three of its
  // Gaussian's deviations, moved down by its offset.
  static gfx::Insets Reach() {
    const int blur = std::ceil(3 * kShadowSigma);
    const int down = std::ceil(kShadowOffset.y());
    return gfx::Insets::TLBR(blur - down, blur, blur + down, blur);
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    gfx::RectF card(GetLocalBounds());
    card.Inset(gfx::InsetsF(Reach()));
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
    // Cast by the card's shape as a layer rounds it, a circle of its radius's
    // circular equivalent (ADR 0057): blurred 32 points, that is the
    // squircle's shadow within a 255th, and Skia blurs a rounded rect once,
    // a corner's worth, where it works a path's blur out again for each tile
    // the window rasters (ADR 0123).
    canvas->DrawRoundRect(card, radius::k2xl * kCircularEquivalent, flags);
  }
};

BEGIN_METADATA(ModalDialog, Shadow)
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
  shadow_ = AddChildView(std::make_unique<Shadow>());
  shadow_->layer()->SetOpacity(0);
  card_ = shadow_->AddChildView(std::make_unique<Card>());
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
  return card_->bounds() + shadow_->bounds().OffsetFromOrigin();
}

bool ModalDialog::Place() {
  scrim_->SetBoundsRect(GetLocalBounds());
  const gfx::Rect shadow_before = shadow_->bounds();
  const gfx::Vector2dF shift_before = shift_;
  const gfx::RectF place = CardPlace();
  const gfx::Rect card(std::floor(place.x()), std::floor(place.y()),
                       std::round(place.width()), std::round(place.height()));
  const gfx::Insets reach = Shadow::Reach();
  gfx::Rect shadow = card;
  shadow.Inset(-reach);
  shadow_->SetBoundsRect(shadow);
  card_->SetBounds(reach.left(), reach.top(), card.width(), card.height());
  content_->SetBounds(kRing + kPadding, kRing + kPadding, width_ - 2 * kPadding,
                      card.height() - 2 * (kRing + kPadding));
  // What the whole points leave of the card's place, on the screen's pixels.
  const float scale = ScaleOf(GetWidget());
  const auto rest = [&](float at, float whole) {
    return std::round((at - whole) * scale) / scale;
  };
  shift_ = gfx::Vector2dF(rest(place.x(), card.x()), rest(place.y(), card.y()));
  return shadow_->bounds() != shadow_before || shift_ != shift_before;
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

gfx::Transform ModalDialog::TransformOf(bool shown) const {
  // The card's place, and its growth about its middle, its shadow's with it.
  gfx::Transform transform =
      gfx::Transform::MakeTranslation(shift_.x(), shift_.y());
  if (!shown) {
    const gfx::PointF middle = gfx::RectF(card_->bounds()).CenterPoint();
    transform.Translate(middle.x(), middle.y());
    transform.Scale(kScale, kScale);
    transform.Translate(-middle.x(), -middle.y());
  }
  return transform;
}

void ModalDialog::Settle() {
  scrim_->layer()->SetOpacity(shown_ ? 1 : 0);
  shadow_->layer()->SetOpacity(shown_ ? 1 : 0);
  shadow_->layer()->SetTransform(TransformOf(shown_));
}

void ModalDialog::Move(bool shown) {
  shown_ = shown;
  moving_ = true;
  // The card and its shadow grow, or shrink, as they fade and the scrim
  // fades with them: on the compositor's animation, from where they stand.
  const Curve curve = motion::Panel();
  const float opacity = shown ? 1 : 0;
  CompositorMotion::Start(
      shadow_->layer(), curve,
      {.opacity = opacity, .transform = TransformOf(shown)});
  CompositorMotion::Start(
      scrim_->layer(), curve, {.opacity = opacity},
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
