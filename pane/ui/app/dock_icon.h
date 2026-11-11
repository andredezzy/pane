// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_DOCK_ICON_H_
#define PANE_UI_APP_DOCK_ICON_H_

#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "pane/model/app_settings.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"

namespace gfx {
class ImageSkia;
}

namespace pane {

class AppIconImages;

// Pane's icon in the Dock while it runs (ADR 0089; the Swift's DockIcon): the
// window the App icon setting names, or for Automatic, Day while the app is
// light and Night while it is dark, as the Theme has it or, under System, the
// Mac. The bundle's own icon is Day, and Night under the system's dark icon
// style; this one holds whatever that style is. A sky's image is decoded off
// the UI thread, so a Theme's change reaches the window first and the Dock
// after it.
class DockIcon : public ui::NativeThemeObserver {
 public:
  // `shown` runs each time the Dock takes another sky's image.
  DockIcon(AppIconImages& images, base::RepeatingClosure shown);
  DockIcon(const DockIcon&) = delete;
  DockIcon& operator=(const DockIcon&) = delete;
  ~DockIcon() override;

  // The setting, now and each time it changes.
  void Show(model::AppIcon setting);
  // The sky the Dock shows, once its image is there.
  std::optional<model::AppIcon> shown() const { return shown_; }

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

 private:
  // The sky for the setting and the app's appearance, once it changes.
  void Apply();
  void Loaded(model::AppIcon sky, gfx::ImageSkia image);
  // The platform's half: the image the Dock shows for the app.
  static void SetImage(const gfx::ImageSkia& image);

  const raw_ref<AppIconImages> images_;
  const base::RepeatingClosure shown_changed_;
  model::AppIcon setting_ = model::AppIcon::kAutomatic;
  // The sky asked for last, and the one the Dock shows.
  std::optional<model::AppIcon> wanted_;
  std::optional<model::AppIcon> shown_;
  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      theme_observation_{this};
  base::WeakPtrFactory<DockIcon> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_APP_DOCK_ICON_H_
