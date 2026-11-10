// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/panels/app_icon_images.h"

#include <optional>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image_skia_rep.h"

namespace pane {

namespace {

// The scales an image is made at: the Mac's displays are 1× or 2×.
constexpr float kScales[] = {1, 2};
constexpr int kSizes[] = {AppIconImages::kRowSize, AppIconImages::kTileSize};

// `bitmap` at `size` points, at each scale, with a filter as fine as Core
// Graphics' own, as the Swift's drew it at the size shown.
gfx::ImageSkia Resized(const SkBitmap& bitmap, int size) {
  gfx::ImageSkia image;
  for (const float scale : kScales) {
    const int pixels = static_cast<int>(size * scale);
    image.AddRepresentation(gfx::ImageSkiaRep(
        skia::ImageOperations::Resize(
            bitmap, skia::ImageOperations::RESIZE_LANCZOS3, pixels, pixels),
        scale));
  }
  image.MakeThreadSafe();
  return image;
}

// "AppIconDay.png": make-icons.swift's names.
std::string FileName(model::AppIcon sky) {
  std::string name(model::AppIconName(sky));
  if (!name.empty()) {
    name[0] = static_cast<char>(name[0] - 'a' + 'A');
  }
  return base::StrCat({"AppIcon", name, ".png"});
}

}  // namespace

// What the pool decoded of one sky.
struct AppIconImages::Decoded {
  model::AppIcon sky = model::AppIcon::kDay;
  // Its thumbnails by size, empty when it couldn't be read.
  std::map<int, gfx::ImageSkia> thumbnails;
  // The Dock's, when asked for.
  gfx::ImageSkia full;
};

const std::vector<model::AppIcon>& AllAppIcons() {
  using model::AppIcon;
  static const base::NoDestructor<std::vector<AppIcon>> icons({
      AppIcon::kAutomatic,
      AppIcon::kDay,
      AppIcon::kClouds,
      AppIcon::kMorning,
      AppIcon::kDusk,
      AppIcon::kFuchsia,
      AppIcon::kSunset,
      AppIcon::kTwilight,
      AppIcon::kAfterglow,
      AppIcon::kNight,
      AppIcon::kMoonrise,
  });
  return *icons;
}

std::u16string AppIconTitle(model::AppIcon icon) {
  std::u16string title = base::UTF8ToUTF16(model::AppIconName(icon));
  if (!title.empty()) {
    title[0] = static_cast<char16_t>(title[0] - u'a' + u'A');
  }
  return title;
}

AppIconImages::AppIconImages() = default;
AppIconImages::~AppIconImages() = default;

gfx::ImageSkia AppIconImages::Thumbnail(model::AppIcon icon, int size) const {
  const auto sky = thumbnails_.find(icon);
  if (sky == thumbnails_.end()) {
    return gfx::ImageSkia();
  }
  const auto found = sky->second.find(size);
  return found == sky->second.end() ? gfx::ImageSkia() : found->second;
}

void AppIconImages::Load(const std::vector<model::AppIcon>& icons) {
  for (const model::AppIcon icon : icons) {
    if (icon == model::AppIcon::kAutomatic) {
      Load({model::AppIcon::kDay, model::AppIcon::kNight});
      continue;
    }
    if (!thumbnails_.contains(icon) && !loading_.contains(icon)) {
      Decode(icon, /*full=*/false, base::OnceCallback<void(gfx::ImageSkia)>());
    }
  }
}

void AppIconImages::LoadAll() {
  Load(AllAppIcons());
}

void AppIconImages::LoadFull(model::AppIcon sky,
                             base::OnceCallback<void(gfx::ImageSkia)> done) {
  Decode(sky, /*full=*/true, std::move(done));
}

void AppIconImages::Decode(model::AppIcon sky,
                           bool full,
                           base::OnceCallback<void(gfx::ImageSkia)> done) {
  const bool thumbnails = !thumbnails_.contains(sky) && !loading_.contains(sky);
  if (thumbnails) {
    loading_.insert(sky);
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(
          [](model::AppIcon sky, bool full, bool thumbnails) {
            Decoded decoded;
            decoded.sky = sky;
            base::FilePath assets;
            if (!base::PathService::Get(base::DIR_ASSETS, &assets)) {
              return decoded;
            }
            const std::optional<std::vector<uint8_t>> bytes =
                base::ReadFileToBytes(assets.AppendASCII(FileName(sky)));
            const SkBitmap bitmap =
                bytes ? gfx::PNGCodec::Decode(*bytes) : SkBitmap();
            if (bitmap.isNull()) {
              LOG(ERROR) << "Could not read " << FileName(sky);
              return decoded;
            }
            if (thumbnails) {
              for (const int size : kSizes) {
                decoded.thumbnails[size] = Resized(bitmap, size);
              }
            }
            if (full) {
              decoded.full = Resized(bitmap, AppIconImages::kDockSize);
            }
            return decoded;
          },
          sky, full, thumbnails),
      base::BindOnce(&AppIconImages::Received, weak_factory_.GetWeakPtr(),
                     std::move(done)));
}

void AppIconImages::Received(base::OnceCallback<void(gfx::ImageSkia)> done,
                             Decoded decoded) {
  if (!decoded.thumbnails.empty()) {
    loading_.erase(decoded.sky);
    thumbnails_[decoded.sky] = std::move(decoded.thumbnails);
    ComposeAutomatic();
    observers_.Notify(&Observer::OnAppIconThumbnailsChanged);
  }
  if (done) {
    std::move(done).Run(std::move(decoded.full));
  }
}

void AppIconImages::ComposeAutomatic() {
  const auto day = thumbnails_.find(model::AppIcon::kDay);
  const auto night = thumbnails_.find(model::AppIcon::kNight);
  if (thumbnails_.contains(model::AppIcon::kAutomatic) ||
      day == thumbnails_.end() || night == thumbnails_.end()) {
    return;
  }
  std::map<int, gfx::ImageSkia>& automatic =
      thumbnails_[model::AppIcon::kAutomatic];
  for (const int size : kSizes) {
    gfx::ImageSkia image;
    for (const float scale : kScales) {
      const int pixels = static_cast<int>(size * scale);
      SkBitmap bitmap;
      bitmap.allocN32Pixels(pixels, pixels);
      bitmap.eraseColor(SK_ColorTRANSPARENT);
      SkCanvas canvas(bitmap);
      // Night whole, then Day over its upper left half: two clipped halves
      // would leave the background showing along their shared edge.
      canvas.drawImage(
          night->second[size].GetRepresentation(scale).GetBitmap().asImage(), 0,
          0);
      const float side = pixels;
      const SkPoint half[] = {{0, side}, {0, 0}, {side, 0}};
      canvas.clipPath(SkPath::Polygon(half, /*isClosed=*/true),
                      /*doAntiAlias=*/true);
      canvas.drawImage(
          day->second[size].GetRepresentation(scale).GetBitmap().asImage(), 0,
          0);
      image.AddRepresentation(gfx::ImageSkiaRep(bitmap, scale));
    }
    automatic[size] = std::move(image);
  }
}

void AppIconImages::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void AppIconImages::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

}  // namespace pane
