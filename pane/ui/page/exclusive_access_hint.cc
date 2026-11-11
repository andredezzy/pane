// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/page/exclusive_access_hint.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/text.h"
#include "pane/ui/view_ids.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/views/controls/label.h"

namespace pane {

namespace {

// 13 medium on a 16-point line, 16 in from the capsule's ends and 10 from
// its top and bottom.
constexpr TextStyle kHintText{13, weight::kMedium, 16};
constexpr int kHorizontal = 16;
constexpr int kVertical = 10;
// How long it stays before it fades.
constexpr base::TimeDelta kShown = base::Seconds(3.5);

}  // namespace

ExclusiveAccessHint::ExclusiveAccessHint() {
  SetID(kViewIdExclusiveAccessHint);
  // Its own layer, which fades without painting again.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  ControlLook look(ControlShape::Capsule());
  look.fill = kPaneColorHint;
  SetBackground(std::make_unique<ControlBackground>(look, nullptr));
  label_ =
      AddChildView(MakeLabel(std::u16string(), kHintText, kPaneColorHintText));
  // It shows, and takes nothing (the Swift's allowsHitTesting(false)).
  SetCanProcessEventsWithinSubtree(false);
  SetVisible(false);
}

ExclusiveAccessHint::~ExclusiveAccessHint() = default;

void ExclusiveAccessHint::SetHint(const std::u16string& hint) {
  if (hint.empty()) {
    hint_.clear();
    fade_.Stop();
    layer()->GetAnimator()->AbortAllAnimations();
    SetVisible(false);
    return;
  }
  if (hint == hint_ && GetVisible()) {
    return;
  }
  hint_ = hint;
  label_->SetText(hint);
  layer()->GetAnimator()->AbortAllAnimations();
  layer()->SetOpacity(1);
  SetVisible(true);
  PreferredSizeChanged();
  fade_.Start(
      FROM_HERE, kShown,
      base::BindOnce(&ExclusiveAccessHint::FadeOut, base::Unretained(this)));
}

void ExclusiveAccessHint::FadeOut() {
  CompositorMotion::Start(layer(), motion::Fade(), {.opacity = 0.0f});
}

gfx::Size ExclusiveAccessHint::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const gfx::Size text = label_->GetPreferredSize();
  return gfx::Size(text.width() + 2 * kHorizontal,
                   text.height() + 2 * kVertical);
}

void ExclusiveAccessHint::Layout(PassKey) {
  label_->SetBounds(kHorizontal, kVertical, width() - 2 * kHorizontal,
                    height() - 2 * kVertical);
}

BEGIN_METADATA(ExclusiveAccessHint)
END_METADATA

}  // namespace pane
