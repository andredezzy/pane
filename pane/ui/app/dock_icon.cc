// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/dock_icon.h"

#include "base/functional/bind.h"
#include "pane/ui/panels/app_icon_images.h"
#include "ui/gfx/image/image_skia.h"

namespace pane {

DockIcon::DockIcon(AppIconImages& images) : images_(images) {
  // The app's appearance follows its Theme and, under System, the Mac's.
  theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
}

DockIcon::~DockIcon() = default;

void DockIcon::Show(model::AppIcon setting) {
  setting_ = setting;
  Apply();
}

void DockIcon::OnNativeThemeUpdated(ui::NativeTheme* observed_theme) {
  Apply();
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
  if (sky != wanted_ || image.isNull()) {
    return;
  }
  SetImage(image);
  shown_ = sky;
}

}  // namespace pane
