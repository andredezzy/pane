// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_PANELS_APP_ICON_PICKER_H_
#define PANE_UI_PANELS_APP_ICON_PICKER_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "pane/model/app_settings.h"
#include "pane/ui/panels/app_icon_images.h"
#include "ui/views/view.h"

namespace pane {

class KeyedButton;

// The App icon picker's content (ADR 0089, Paper 06.2; the Swift's
// AppIconPicker), in its dialog over the window: "App icon" and what
// Automatic does, then every sky six a row, each its icon at 56 points over
// its name, the chosen one ringed, and Done. A click on a sky chooses it at
// once and the Dock follows, so the picker stays open to compare; Done,
// Return, Esc, or a click outside closes it (ModalDialog). The pointer over a
// sky fades its tile in; choosing is instant.
class AppIconPicker : public views::View, public AppIconImages::Observer {
  METADATA_HEADER(AppIconPicker, views::View)

 public:
  // The dialog's width, its content 24 in.
  static constexpr int kWidth = 480;

  AppIconPicker(AppIconImages& images,
                model::AppIcon selection,
                base::RepeatingCallback<void(model::AppIcon)> choose,
                base::RepeatingClosure done);
  AppIconPicker(const AppIconPicker&) = delete;
  AppIconPicker& operator=(const AppIconPicker&) = delete;
  ~AppIconPicker() override;

  // The sky ringed, as the setting has it.
  void SetSelection(model::AppIcon selection);
  // Each sky's tile by the Swift's names ("pane.settings.appIcon.dusk"), and
  // Done, for the channel.
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

  // AppIconImages::Observer:
  void OnAppIconThumbnailsChanged() override;

 private:
  class Tile;

  const raw_ref<AppIconImages> images_;
  raw_ptr<views::View> heading_ = nullptr;
  std::map<model::AppIcon, raw_ptr<Tile>> tiles_;
  raw_ptr<KeyedButton> done_ = nullptr;
  base::ScopedObservation<AppIconImages, AppIconImages::Observer>
      images_observation_{this};
};

}  // namespace pane

#endif  // PANE_UI_PANELS_APP_ICON_PICKER_H_
