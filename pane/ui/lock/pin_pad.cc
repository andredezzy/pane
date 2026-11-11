// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/lock/pin_pad.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/text.h"
#include "pane/ui/view_ids.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The foreground at `alpha`, as SwiftUI's .opacity(alpha) of it, which an
// animation moves along.
SkColor ForegroundAt(SkColor foreground, double alpha) {
  return SkColorSetA(
      foreground,
      static_cast<SkAlpha>(
          std::clamp(alpha, 0.0, 1.0) * SkColorGetA(foreground) + 0.5));
}

// A dot's ring: 1.5 points just inside its edge, the foreground at 25%.
constexpr float kDotRing = 1.5f;
constexpr double kDotRingAlpha = 0.25;

// The shake runs through its 0.4 s at an even pace; its steps ease
// themselves (ShakeOffset).
const Curve& ShakeTime() {
  static const base::NoDestructor<Curve> time(
      Curve::Cubic(0, 0, 1, 1, motion::kShakeDuration));
  return *time;
}

// A digit: 26 points, SwiftUI's thin, the foreground at 85%, all of it lit;
// its ring 1 point just inside its edge at 12%, 20% lit; filled at 10% while
// pressed (numpad.tsx).
constexpr TextStyle kDigitText{26, weight::kThin, 0};
constexpr double kDigitAlpha = 0.85;
constexpr double kRingAlpha = 0.12;
constexpr double kRingLitAlpha = 0.2;
constexpr double kPressAlpha = 0.1;

// Delete's glyph: numpad.tsx's 24 × 20, drawn at 22 × 18 in the key's middle,
// stroked 1.2, the foreground at 35%, 60% under the pointer, 80% pressed.
constexpr float kGlyphWidth = 22;
constexpr float kGlyphHeight = 18;
constexpr float kGlyphStroke = 1.2f;
constexpr double kGlyphAlpha = 0.35;
constexpr double kGlyphLitAlpha = 0.6;
constexpr double kGlyphPressedAlpha = 0.8;

const SkPath& DeleteGlyph() {
  static const base::NoDestructor<SkPath> glyph([] {
    SkPathBuilder path;
    path.moveTo(9, 2);
    path.lineTo(20, 2);
    path.arcTo({22, 2}, {22, 4}, 2);
    path.lineTo(22, 16);
    path.arcTo({22, 18}, {20, 18}, 2);
    path.lineTo(9, 18);
    path.lineTo(2, 10);
    path.close();
    path.moveTo(16, 7);
    path.lineTo(12, 13);
    path.moveTo(12, 7);
    path.lineTo(16, 13);
    const SkMatrix scale =
        SkMatrix::Scale(kGlyphWidth / 24.0f, kGlyphHeight / 20.0f);
    return path.detach(&scale);
  }());
  return *glyph;
}

}  // namespace

// A dot's fill, and how far it stands from its place in the row while it
// slides there.
struct PinDots::Dot {
  explicit Dot(PinDots* dots, double filled)
      : fill(filled,
             base::BindRepeating(
                 [](PinDots* dots, double) { dots->SchedulePaint(); },
                 base::Unretained(dots))),
        shift(0,
              base::BindRepeating(
                  [](PinDots* dots, double) { dots->SchedulePaint(); },
                  base::Unretained(dots))) {}

  ValueMotion fill;
  ValueMotion shift;
};

PinDots::PinDots()
    : shake_(0, base::BindRepeating(&PinDots::Shook, base::Unretained(this))) {
  SetID(kViewIdLockScreenDots);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetCanProcessEventsWithinSubtree(false);
  // One element, read as how many digits it holds of how many.
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
}

PinDots::~PinDots() = default;

::ui::Compositor* PinDots::compositor() {
  views::Widget* widget = GetWidget();
  return widget && widget->IsVisible() ? widget->GetCompositor() : nullptr;
}

void PinDots::Set(int count, int filled, bool animate) {
  ::ui::Compositor* compositor = animate ? this->compositor() : nullptr;
  const int before = this->count();
  if (count != before) {
    // The dots that stay slide from where the row had them, the row's middle
    // moved by half its change; a new one is there at once, as it is.
    const double moved = (count - before) * (kSize + kGap) / 2.0;
    if (count < before) {
      dots_.resize(count);
    }
    for (std::unique_ptr<Dot>& dot : dots_) {
      dot->shift.MoveTo(dot->shift.value() + moved, motion::Dot(), nullptr);
      dot->shift.MoveTo(0, motion::Dot(), compositor);
    }
    for (int index = before; index < count; ++index) {
      dots_.push_back(std::make_unique<Dot>(this, index < filled ? 1 : 0));
    }
  }
  for (int index = 0; index < count; ++index) {
    ValueMotion& fill = dots_[index]->fill;
    const double target = index < filled ? 1 : 0;
    if (target != fill.target()) {
      fill.MoveTo(target, motion::Dot(), compositor);
    }
  }
  if (count != before || filled != filled_) {
    GetViewAccessibility().SetName(
        base::StrCat({base::NumberToString16(filled), u" of ",
                      base::NumberToString16(count), u" digits"}));
    SchedulePaint();
  }
  filled_ = filled;
}

void PinDots::Shake() {
  ::ui::Compositor* compositor = this->compositor();
  if (!compositor || motion::IsReduced()) {
    return;
  }
  shake_.MoveTo(0, ShakeTime(), nullptr);
  shake_.MoveTo(1, ShakeTime(), compositor);
}

void PinDots::Shook(double share) {
  layer()->SetTransform(gfx::Transform::MakeTranslation(
      motion::ShakeOffset(motion::kShakeDuration * share), 0));
}

gfx::Size PinDots::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(std::max(count() * (kSize + kGap) - kGap, 0), kSize);
}

void PinDots::OnPaint(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors || dots_.empty()) {
    return;
  }
  const SkColor foreground = colors->GetColor(kPaneColorForeground);
  const float row = count() * (kSize + kGap) - kGap;
  // In the view's middle, to the fraction of a point.
  const float left = (width() - row) / 2.0f;
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  for (int index = 0; index < count(); ++index) {
    const Dot& dot = *dots_[index];
    const gfx::PointF center(
        left + index * (kSize + kGap) + dot.shift.value() + kSize / 2.0f,
        kSize / 2.0f);
    const double fill = dot.fill.value();
    if (fill > 0) {
      flags.setStyle(cc::PaintFlags::kFill_Style);
      flags.setColor(ForegroundAt(foreground, fill));
      canvas->DrawCircle(center, kSize / 2.0f, flags);
    }
    // The ring fades as the fill comes, as SwiftUI's overlay left.
    if (fill < 1) {
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(kDotRing);
      flags.setColor(ForegroundAt(foreground, kDotRingAlpha * (1 - fill)));
      canvas->DrawCircle(center, (kSize - kDotRing) / 2.0f, flags);
    }
  }
}

BEGIN_METADATA(PinDots)
END_METADATA

class Numpad::DigitKey : public views::Button {
  METADATA_HEADER(DigitKey, views::Button)

 public:
  DigitKey(char16_t digit, base::RepeatingCallback<void(char16_t)> pressed)
      : views::Button(base::BindRepeating(
            [](const base::RepeatingCallback<void(char16_t)>& pressed,
               char16_t digit) { pressed.Run(digit); },
            std::move(pressed),
            digit)),
        digit_(digit) {
    SetID(kViewIdLockScreenDigit);
    GetViewAccessibility().SetName(std::u16string(1, digit));
    label_ = AddChildView(
        MakeLabel(std::u16string(1, digit), kDigitText, kPaneColorForeground));
    label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    label_->SetCanProcessEventsWithinSubtree(false);
    label_->GetViewAccessibility().SetIsIgnored(true);
    highlight_ = std::make_unique<ControlHighlight>(
        this, ControlShape::Circle(), /*disabled_opacity=*/1,
        base::BindRepeating(&DigitKey::Changed, base::Unretained(this)));
    highlight_->SetPressFadesIn(true);
  }

  char16_t digit() const { return digit_; }

  // views::Button:
  void Layout(PassKey) override { label_->SetBoundsRect(GetLocalBounds()); }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    Changed();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const SkColor foreground = colors->GetColor(kPaneColorForeground);
    const gfx::RectF bounds(GetLocalBounds());
    const ControlShape circle = ControlShape::Circle();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (highlight_->press() > 0) {
      flags.setColor(
          ForegroundAt(foreground, kPressAlpha * highlight_->press()));
      canvas->DrawPath(circle.PathIn(bounds), flags);
    }
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(ForegroundAt(
        foreground,
        kRingAlpha + (kRingLitAlpha - kRingAlpha) * highlight_->hover()));
    canvas->DrawPath(circle.StrokePathIn(bounds, 1), flags);
  }

 private:
  // The number lights with the ring.
  void Changed() {
    if (label_) {
      if (const ui::ColorProvider* colors = GetColorProvider()) {
        label_->SetEnabledColor(ForegroundAt(
            colors->GetColor(kPaneColorForeground),
            kDigitAlpha +
                (1 - kDigitAlpha) * (highlight_ ? highlight_->hover() : 0)));
      }
    }
    SchedulePaint();
  }

  const char16_t digit_;
  raw_ptr<views::Label> label_ = nullptr;
  std::unique_ptr<ControlHighlight> highlight_;
};

BEGIN_METADATA(Numpad, DigitKey)
END_METADATA

class Numpad::DeleteKey : public views::Button {
  METADATA_HEADER(DeleteKey, views::Button)

 public:
  explicit DeleteKey(base::RepeatingClosure pressed)
      : views::Button(std::move(pressed)) {
    SetID(kViewIdLockScreenBackspace);
    GetViewAccessibility().SetName(u"Delete");
    highlight_ = std::make_unique<ControlHighlight>(
        this, ControlShape::Circle(), /*disabled_opacity=*/1,
        base::BindRepeating(&DeleteKey::SchedulePaint, base::Unretained(this)));
    highlight_->SetPressFadesIn(true);
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    // 35% at rest, 60% lit, 80% pressed, each change fading.
    const double alpha =
        kGlyphAlpha + (kGlyphLitAlpha - kGlyphAlpha) * highlight_->hover() +
        (kGlyphPressedAlpha - kGlyphLitAlpha) * highlight_->press();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(kGlyphStroke);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
    flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
    flags.setColor(ForegroundAt(colors->GetColor(kPaneColorForeground), alpha));
    canvas->DrawPath(DeleteGlyph().makeOffset((width() - kGlyphWidth) / 2,
                                              (height() - kGlyphHeight) / 2),
                     flags);
  }

 private:
  std::unique_ptr<ControlHighlight> highlight_;
};

BEGIN_METADATA(Numpad, DeleteKey)
END_METADATA

Numpad::Numpad(base::RepeatingCallback<void(char16_t)> digit,
               base::RepeatingClosure backspace) {
  // 1 to 9, then 0.
  for (const char16_t key : u"1234567890") {
    if (key) {
      digits_.push_back(AddChildView(std::make_unique<DigitKey>(key, digit)));
    }
  }
  delete_ = AddChildView(std::make_unique<DeleteKey>(std::move(backspace)));
}

Numpad::~Numpad() = default;

std::vector<std::pair<std::string, const views::View*>> Numpad::NamedViews()
    const {
  std::vector<std::pair<std::string, const views::View*>> named;
  for (const DigitKey* key : digits_) {
    named.emplace_back(
        base::StrCat({ViewIdName(kViewIdLockScreenDigit), ".",
                      base::UTF16ToUTF8(std::u16string(1, key->digit()))}),
        key);
  }
  named.emplace_back(std::string(ViewIdName(kViewIdLockScreenBackspace)),
                     delete_);
  return named;
}

gfx::Size Numpad::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(kWidth, kHeight);
}

void Numpad::Layout(PassKey) {
  const auto place = [](views::View* key, int column, int row) {
    key->SetBounds(column * (kKey + kGap), row * (kKey + kGap), kKey, kKey);
  };
  // Three rows of three, then 0 in the middle of the last and Delete after.
  for (size_t index = 0; index < 9; ++index) {
    place(digits_[index], index % 3, index / 3);
  }
  place(digits_[9], 1, 3);
  place(delete_, 2, 3);
}

BEGIN_METADATA(Numpad)
END_METADATA

}  // namespace pane
