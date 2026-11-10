// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/update_pill.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/focus_ring.h"
#include "pane/ui/design/icon_button.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/view_ids.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The dot, 6 points, 7 in; the text 10 after it (sidebar.tsx's gap-2.5).
constexpr int kDotInset = 7;
constexpr int kDotSize = 6;
constexpr int kTextX = kDotInset + kDotSize + 10;
// The text clear of the dismiss button (sidebar.tsx's pr-6).
constexpr int kTextTrailing = 24;
// While it downloads: the bar 14 after the text (the row's 10, then 4),
// running out to 7 from the pill's end, at least 24 long, and drawing back
// 25 under the pointer, as far from the dismiss button as the text is from
// the bar.
constexpr int kBarGap = 14;
constexpr int kBarEdge = 7;
constexpr int kBarMinimum = 24;
constexpr double kBarBack = 25;
constexpr int kBarHeight = 4;
// The dismiss button: a 12-point X in 16 points, 6 from the pill's end.
constexpr int kDismissSize = 16;
constexpr int kDismissEdge = 6;
// Coming and going, it rises from 4 points lower.
constexpr float kAppearOffset = 4;
// The dot at half, at the bottom of its breath.
constexpr double kPulseLow = 0.5;

}  // namespace

// The download's bar: a 4-point track, the foreground at 10%, with the
// emerald share downloaded, moving to each new share on Motion.progress.
class UpdatePill::Bar : public views::View {
  METADATA_HEADER(Bar, views::View)

 public:
  Bar()
      : share_(
            0,
            base::BindRepeating([](Bar* bar, double) { bar->SchedulePaint(); },
                                base::Unretained(this))) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetOpacity(0);
  }

  void SetShare(double share, ui::Compositor* compositor) {
    share_.MoveTo(std::clamp(share, 0.0, 1.0), motion::Progress(), compositor);
  }
  // Where the track starts within the view, to the fraction of a point the
  // text before it ends at, as SwiftUI lays it out.
  void SetLeading(float leading) {
    if (leading != leading_) {
      leading_ = leading;
      SchedulePaint();
    }
  }
  // A new download starts empty.
  void Reset() { share_.MoveTo(0, motion::Progress(), nullptr); }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const ControlShape capsule = ControlShape::Capsule();
    gfx::RectF track(GetLocalBounds());
    track.Inset(gfx::InsetsF::TLBR(0, leading_, 0, 0));
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kPaneColorUpdateTrack));
    canvas->DrawPath(capsule.PathIn(track), flags);
    if (share_.value() <= 0) {
      return;
    }
    flags.setColor(colors->GetColor(kPaneColorSuccess));
    canvas->DrawPath(capsule.PathIn(gfx::RectF(track.x(), track.y(),
                                               track.width() * share_.value(),
                                               track.height())),
                     flags);
  }

 private:
  ValueMotion share_;
  float leading_ = 0;
};

BEGIN_METADATA(UpdatePill, Bar)
END_METADATA

// The pill's click, under its dot, text, and bar: a plain button, its look
// the pill's own (the Swift's .plain style).
class UpdatePill::Press : public views::Button {
  METADATA_HEADER(Press, views::Button)

 public:
  explicit Press(PressedCallback pressed) : views::Button(std::move(pressed)) {}
};

BEGIN_METADATA(UpdatePill, Press)
END_METADATA

UpdatePill::UpdatePill(base::RepeatingClosure press,
                       base::RepeatingClosure dismiss)
    : appear_(
          0,
          base::BindRepeating(&UpdatePill::Appeared, base::Unretained(this))),
      pulse_(1,
             base::BindRepeating(&UpdatePill::Pulsed, base::Unretained(this))),
      label_in_(1,
                base::BindRepeating(
                    [](UpdatePill* pill, double share) {
                      pill->label_->layer()->SetOpacity(share);
                      pill->leaving_label_->layer()->SetOpacity(1 - share);
                      pill->leaving_label_->SetVisible(share < 1);
                    },
                    base::Unretained(this))),
      bar_shown_(0,
                 base::BindRepeating(
                     [](UpdatePill* pill, double share) {
                       pill->bar_->layer()->SetOpacity(share);
                       const bool visible =
                           share > 0 || pill->update_.downloading;
                       if (visible != pill->bar_->GetVisible()) {
                         pill->bar_->SetVisible(visible);
                         pill->InvalidateLayout();
                       }
                     },
                     base::Unretained(this))),
      bar_back_(0,
                base::BindRepeating(
                    [](UpdatePill* pill, double) { pill->InvalidateLayout(); },
                    base::Unretained(this))),
      dismiss_shown_(0,
                     base::BindRepeating(
                         [](UpdatePill* pill, double share) {
                           pill->dismiss_->layer()->SetOpacity(share);
                         },
                         base::Unretained(this))) {
  SetID(kViewIdUpdatePill);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  // The pointer over the pill, its button and dismiss included.
  SetNotifyEnterExitOnChild(true);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetOpacity(0);

  // The click: what the pill's status offers.
  button_ = AddChildView(std::make_unique<Press>(std::move(press)));
  button_->SetID(kViewIdUpdate);
  views::InkDrop::Get(button_)->SetMode(views::InkDropHost::InkDropMode::OFF);
  views::FocusRing::Remove(button_);
  FocusRing::Install(button_, ControlShape::Squircle(radius::kLg));

  // The emerald dot, the same in both appearances.
  dot_ = button_->AddChildView(std::make_unique<views::View>());
  dot_->SetBackground(
      views::CreateRoundedRectBackground(kPaneColorSuccess, kDotSize / 2.0f));
  dot_->SetPaintToLayer();
  dot_->layer()->SetFillsBoundsOpaquely(false);
  dot_->SetCanProcessEventsWithinSubtree(false);

  for (raw_ptr<views::Label>* label : {&leaving_label_, &label_}) {
    *label = button_->AddChildView(MakeLabel(std::u16string(), text::kSidebar,
                                             kPaneColorAccentForeground));
    (*label)->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    (*label)->SetCanProcessEventsWithinSubtree(false);
    (*label)->SetPaintToLayer();
    (*label)->layer()->SetFillsBoundsOpaquely(false);
  }
  leaving_label_->SetVisible(false);
  leaving_label_->GetViewAccessibility().SetIsIgnored(true);

  bar_ = button_->AddChildView(std::make_unique<Bar>());
  bar_->SetCanProcessEventsWithinSubtree(false);
  bar_->SetVisible(false);

  ControlLook dismiss_look =
      IconButtonLook(ControlShape::Squircle(radius::kXs));
  // Its tint alone lights under the pointer.
  dismiss_look.hover_fill.reset();
  dismiss_ = AddChildView(std::make_unique<IconButton>(
      std::move(dismiss), kDismissSize, IconButton::Glyph{Icon::kX, 12, 2},
      u"Dismiss", dismiss_look));
  dismiss_->SetID(kViewIdUpdateDismiss);
  dismiss_->SetPaintToLayer();
  dismiss_->layer()->SetFillsBoundsOpaquely(false);
  dismiss_->layer()->SetOpacity(0);
}

UpdatePill::~UpdatePill() = default;

const views::View* UpdatePill::button() const {
  return button_;
}

const views::View* UpdatePill::dismiss() const {
  return dismiss_;
}

ui::Compositor* UpdatePill::compositor() {
  views::Widget* widget = GetWidget();
  return widget ? widget->GetCompositor() : nullptr;
}

void UpdatePill::Set(const SidebarUpdate& update) {
  const SidebarUpdate before = std::exchange(update_, update);
  ui::Compositor* compositor = this->compositor();

  // The text crossfades into the new status, once the bar has gone when it
  // goes (Motion.statusAfterBar).
  if (update.label != before.label) {
    label_->SetText(update.label);
    button_->GetViewAccessibility().SetName(update.label);
    if (before.label.empty()) {
      label_in_.MoveTo(1, motion::Status(), nullptr);
    } else {
      leaving_label_->SetText(before.label);
      label_in_.MoveTo(0, motion::Status(), nullptr);
      label_in_.MoveTo(
          1, update.downloaded ? motion::StatusAfterBar() : motion::Status(),
          compositor);
    }
  }

  // The bar fades in with the download and out at its end
  // (Motion.barExit), starting empty each time.
  if (update.downloading != before.downloading) {
    if (update.downloading) {
      bar_->Reset();
      bar_->SetVisible(true);
    }
    bar_shown_.MoveTo(update.downloading ? 1 : 0,
                      update.downloading ? motion::Status() : motion::BarExit(),
                      compositor);
  }
  if (update.downloading) {
    bar_->SetShare(update.progress, compositor);
  }
  UpdatePulse();
  InvalidateLayout();
}

void UpdatePill::Show(bool shown, base::OnceClosure gone) {
  shown_ = shown;
  gone_ = std::move(gone);
  appear_.MoveTo(shown ? 1 : 0, motion::Fade(), compositor());
}

void UpdatePill::Appeared(double share) {
  layer()->SetOpacity(share);
  layer()->SetTransform(
      gfx::Transform::MakeTranslation(0, (1 - share) * kAppearOffset));
  if (!shown_ && share <= 0 && gone_) {
    std::move(gone_).Run();
  }
}

bool UpdatePill::Pulses() {
  return update_.downloading && compositor() && !motion::IsReduced();
}

void UpdatePill::UpdatePulse() {
  if (Pulses()) {
    if (!pulse_.is_moving()) {
      pulse_.MoveTo(pulse_.value() < 1 ? 1 : kPulseLow, motion::Pulse(),
                    compositor());
    }
    return;
  }
  pulse_.MoveTo(1, motion::Pulse(), nullptr);
}

// Tailwind's animate-pulse: to half and back, every two seconds, for as long
// as the download runs. Each half starts once the last has landed.
void UpdatePill::Pulsed(double opacity) {
  dot_->layer()->SetOpacity(opacity);
  if (pulse_.is_moving() || !Pulses()) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&UpdatePill::UpdatePulse, weak_factory_.GetWeakPtr()));
}

void UpdatePill::SetHovered(bool hovered) {
  if (hovered == hovered_) {
    return;
  }
  hovered_ = hovered;
  ui::Compositor* compositor = this->compositor();
  // The bar draws back at once and the button shows once it has; on the
  // way out the button goes first and the bar runs out after.
  dismiss_shown_.MoveTo(
      hovered ? 1 : 0,
      hovered && update_.downloading ? motion::HoverTurn() : motion::Hover(),
      compositor);
  bar_back_.MoveTo(hovered ? kBarBack : 0,
                   hovered ? motion::Hover() : motion::HoverTurn(), compositor);
}

void UpdatePill::OnMouseEntered(const ui::MouseEvent& event) {
  SetHovered(true);
}

void UpdatePill::OnMouseExited(const ui::MouseEvent& event) {
  SetHovered(false);
}

void UpdatePill::AddedToWidget() {
  UpdatePulse();
}

void UpdatePill::Layout(PassKey) {
  button_->SetBoundsRect(GetLocalBounds());
  dot_->SetBounds(kDotInset, (height() - kDotSize) / 2, kDotSize, kDotSize);

  const int line = text::kSidebar.line_height;
  const int top = (height() - line) / 2;
  // Whole beside the bar, which takes what is left: the text's own width,
  // but never into the bar's least room.
  const int text_room = bar_->GetVisible()
                            ? width() - kTextX - kBarGap - kBarMinimum -
                                  static_cast<int>(kBarBack) - kBarEdge
                            : width() - kTextX - kTextTrailing;
  for (views::Label* label : {label_.get(), leaving_label_.get()}) {
    label->SetBounds(kTextX, top,
                     std::clamp(label->GetPreferredSize().width(), 0,
                                std::max(text_room, 0)),
                     line);
  }
  // The bar 14 after the text's own width, to the pixel, where the label's
  // view is whole points wide.
  const views::Widget* widget = GetWidget();
  const float scale = widget && widget->GetCompositor()
                          ? widget->GetCompositor()->device_scale_factor()
                          : 2;
  const float text_width =
      std::min(gfx::GetStringWidthF(label_->GetText(), label_->font_list()),
               static_cast<float>(label_->width()));
  // SwiftUI rounds a text's width up to the pixel.
  const float bar_x = kTextX + std::ceil(text_width * scale) / scale + kBarGap;
  const int bar_left = static_cast<int>(std::floor(bar_x));
  const int bar_end =
      width() - kBarEdge - static_cast<int>(std::round(bar_back_.value()));
  bar_->SetBounds(bar_left, (height() - kBarHeight) / 2,
                  std::max(bar_end - bar_left, 0), kBarHeight);
  bar_->SetLeading(bar_x - bar_left);

  dismiss_->SetBounds(width() - kDismissEdge - kDismissSize,
                      (height() - kDismissSize) / 2, kDismissSize,
                      kDismissSize);
}

// The pill's fill: the foreground at 5%, rounded 15.
void UpdatePill::OnPaint(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(colors->GetColor(kPaneColorUpdatePill));
  canvas->DrawPath(SquirclePath(gfx::RectF(GetLocalBounds()), radius::kLg),
                   flags);
}

BEGIN_METADATA(UpdatePill)
END_METADATA

}  // namespace pane
