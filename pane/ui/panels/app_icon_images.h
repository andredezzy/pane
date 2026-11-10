// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_PANELS_APP_ICON_IMAGES_H_
#define PANE_UI_PANELS_APP_ICON_IMAGES_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "pane/model/app_settings.h"
#include "ui/gfx/image/image_skia.h"

namespace pane {

// The window Pane's icon is, in each of its ten skies (ADR 0089): as the Dock
// shows it, and as Settings shows it, at the row's 36 points and a tile's
// 56, at 1× and 2×, each from macOS's own drawing of an app whose icon is
// that window (its body and shadow on a 1024-pixel canvas, made by
// app/icon/make-icons.swift). Automatic's is Night with Day over
// its upper left half, as the Swift's DockIcon drew it. The images ship with
// the app (pane/ui/panels/app_icons, in the framework's Resources on the
// Mac), and each is read and decoded off the UI thread once asked for: the
// Dock's at launch, the row's with it, and every tile's when Settings first
// shows.
class AppIconImages {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // More thumbnails are ready to show.
    virtual void OnAppIconThumbnailsChanged() = 0;
  };

  // Settings' sizes, in points: the App icon row's, and a picker tile's.
  static constexpr int kRowSize = 36;
  static constexpr int kTileSize = 56;
  // The Dock's tile, the largest the Dock and the app switcher draw an app's
  // icon: AppKit draws the image it's given twice at each change, into the
  // app's icon and into the tile, which took 15.6 ms of the UI thread at
  // the whole 1024 pixels, as the Swift's NSImage(named:) did.
  static constexpr int kDockSize = 128;

  AppIconImages();
  AppIconImages(const AppIconImages&) = delete;
  AppIconImages& operator=(const AppIconImages&) = delete;
  ~AppIconImages();

  // `icon`'s thumbnail at `size` points (kRowSize or kTileSize): empty
  // until it is decoded.
  gfx::ImageSkia Thumbnail(model::AppIcon icon, int size) const;
  // Decodes the thumbnails of `icons` that aren't decoded or on their way;
  // Automatic's are Day's and Night's.
  void Load(const std::vector<model::AppIcon>& icons);
  // Every sky's, for the picker.
  void LoadAll();
  // `sky` (not Automatic) as the Dock shows it, kDockSize points at 1× and
  // 2×, decoded for `done`, which hears an empty image when it can't be
  // read; its thumbnails come too.
  void LoadFull(model::AppIcon sky,
                base::OnceCallback<void(gfx::ImageSkia)> done);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  struct Decoded;

  void Decode(model::AppIcon sky,
              bool full,
              base::OnceCallback<void(gfx::ImageSkia)> done);
  void Received(base::OnceCallback<void(gfx::ImageSkia)> done, Decoded decoded);
  // Automatic's, once Day's and Night's are both there.
  void ComposeAutomatic();

  // Each sky's, by its size in points.
  std::map<model::AppIcon, std::map<int, gfx::ImageSkia>> thumbnails_;
  // The skies whose thumbnails are on their way.
  std::set<model::AppIcon> loading_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<AppIconImages> weak_factory_{this};
};

// The skies in Settings' order, the sky through a day, Automatic first.
const std::vector<model::AppIcon>& AllAppIcons();

// What the setting's names read as: "Automatic", "Day".
std::u16string AppIconTitle(model::AppIcon icon);

}  // namespace pane

#endif  // PANE_UI_PANELS_APP_ICON_IMAGES_H_
