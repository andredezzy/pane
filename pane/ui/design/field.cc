// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/field.h"

#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/focus_ring.h"
#include "pane/ui/design/squircle.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/render_text.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/border.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// A disabled field, as a whole.
constexpr float kDisabledOpacity = 0.5f;

}  // namespace

SelectionFollowsWindow::SelectionFollowsWindow(views::Textfield* field)
    : field_(field) {
  view_observation_.Observe(field);
  Follow(field->GetWidget());
}

SelectionFollowsWindow::~SelectionFollowsWindow() = default;

void SelectionFollowsWindow::OnViewAddedToWidget(views::View* view) {
  Follow(view->GetWidget());
}

void SelectionFollowsWindow::OnViewRemovedFromWidget(views::View* view) {
  widget_observation_.Reset();
}

void SelectionFollowsWindow::OnWidgetActivationChanged(views::Widget* widget,
                                                       bool active) {
  field_->SetSelectionBackgroundColorId(
      active ? kPaneColorTextSelection : kPaneColorTextSelectionInactive);
  // The selected text keeps the field's own color, as an NSTextField's does,
  // where Chromium's takes its own (kColorTextfieldSelectionForeground).
  field_->SetSelectionTextColorId(field_->text_color_id());
}

void SelectionFollowsWindow::OnWidgetDestroying(views::Widget* widget) {
  widget_observation_.Reset();
}

void SelectionFollowsWindow::Follow(views::Widget* widget) {
  widget_observation_.Reset();
  if (!widget) {
    return;
  }
  widget_observation_.Observe(widget);
  OnWidgetActivationChanged(widget, widget->IsActive());
}

Field::Field(ui::ColorVariant fill, const TextStyle& style)
    : fill_(std::move(fill)),
      problem_shown_(0,
                     base::BindRepeating(
                         [](Field* field, double) { field->SchedulePaint(); },
                         base::Unretained(this))) {
  // Its fill and border are this one's, painted below and over the text, so
  // Chromium's own background, border, ring, and hover go.
  SetBackgroundEnabled(false);
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, kTextInset)));
  views::FocusRing::Remove(this);
  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
  ring_ = FocusRing::Install(this, ControlShape::Squircle(kRadius));
  ring_->SetShowsWhen(base::BindRepeating([](const views::View* field) {
    return !static_cast<const Field*>(field)->has_problem();
  }));

  SetFontList(SystemFont(style.size, style.weight));
  SetTextColorId(kPaneColorForeground);
  SetPlaceholderTextColorId(kPaneColorMutedForeground);
  // Grayscale rather than subpixel text, as StyleLabel's (ADR 0060).
  GetRenderText()->set_subpixel_rendering_suppressed(true);

  enabled_subscription_ = AddEnabledChangedCallback(
      base::BindRepeating(&Field::EnabledChanged, base::Unretained(this)));
}

Field::~Field() = default;

void Field::SetProblem(std::optional<std::u16string> problem) {
  if (problem_ == problem) {
    return;
  }
  const bool had = problem_.has_value();
  problem_ = std::move(problem);
  SetInvalid(problem_.has_value());
  GetViewAccessibility().SetDescription(problem_.value_or(u""));
  if (had != problem_.has_value()) {
    problem_shown_.MoveTo(problem_ ? 1 : 0, motion::Toggle(),
                          GetWidget() ? GetWidget()->GetCompositor() : nullptr);
    ring_->Refresh();
  }
}

gfx::Size Field::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(
      views::Textfield::CalculatePreferredSize(available_size).width(),
      kHeight);
}

void Field::OnPaintBackground(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(fill_.ResolveToSkColor(colors));
  canvas->DrawPath(SquirclePath(gfx::RectF(GetLocalBounds()), kRadius), flags);
}

void Field::OnPaintBorder(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1);
  flags.setColor(Mix(colors->GetColor(kPaneColorBorder),
                     colors->GetColor(kPaneColorDestructive),
                     problem_shown_.value()));
  canvas->DrawPath(SquircleStrokePath(gfx::RectF(GetLocalBounds()), kRadius, 1),
                   flags);
}

void Field::EnabledChanged() {
  if (!GetEnabled()) {
    if (!layer()) {
      SetPaintToLayer();
      layer()->SetFillsBoundsOpaquely(false);
      made_layer_ = true;
    }
    layer()->SetOpacity(kDisabledOpacity);
  } else if (layer()) {
    if (std::exchange(made_layer_, false)) {
      DestroyLayer();
    } else {
      layer()->SetOpacity(1);
    }
  }
}

BEGIN_METADATA(Field)
END_METADATA

}  // namespace pane
