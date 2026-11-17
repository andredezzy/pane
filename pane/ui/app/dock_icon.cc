// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/dock_icon.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "pane/ui/panels/app_icon_images.h"
#include "ui/gfx/image/image_skia.h"

namespace pane {

DockIcon::DockIcon(AppIconImages& images, base::RepeatingClosure shown)
    : images_(images), shown_changed_(std::move(shown)) {
  // The app's appearance follows its Theme and, under System, the Mac's.
  theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
}

DockIcon::~DockIcon() = default;

void DockIcon::Show(model::AppIcon setting) {
  setting_ = setting;
  Apply();
}

// Read on the turn after the notification, once every observer of the
// theme's change has run and what it changed has settled: on Windows the
// Automatic sky once stayed Night for minutes after Windows went light, the
// window light (W9a, windows-install in the test VM), where a read at the
// notification found the sky it already wanted and did nothing. Several
// notifications in a turn read once.
void DockIcon::OnNativeThemeUpdated(ui::NativeTheme* observed_theme) {
  if (std::exchange(apply_posted_, true)) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<DockIcon> icon) {
                       if (icon) {
                         icon->apply_posted_ = false;
                         icon->Apply();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

void DockIcon::Apply() {
  const bool dark =
      ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==
      ui::NativeTheme::PreferredColorScheme::kDark;
  const model::AppIcon sky = model::Resolved(setting_, dark);
  // The window on show, or on its way, redraws nothing.
  if (sky == wanted_) {
    return;
  }
  wanted_ = sky;
  images_->LoadFull(
      sky, base::BindOnce(&DockIcon::Loaded, weak_factory_.GetWeakPtr(), sky));
}

void DockIcon::Loaded(model::AppIcon sky, gfx::ImageSkia image) {
  // Another sky was asked for meanwhile.
  if (sky != wanted_) {
    return;
  }
  // Its image wouldn't read: the sky shown stays, and the next change asks
  // again, where the sky stayed wanted, and every change after found it
  // wanted already.
  if (image.isNull()) {
    wanted_ = shown_;
    return;
  }
  SetImage(image);
  if (shown_ != sky) {
    FollowInShortcuts(sky);
  }
  shown_ = sky;
  if (shown_changed_) {
    shown_changed_.Run();
  }
}

}  // namespace pane
